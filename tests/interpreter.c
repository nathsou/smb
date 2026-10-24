// A reference for recompiled programs that drive the hardware registers: a
// plain 6502 interpreter over the same runtime (memory map, PPU registers,
// mapper, APU), with the same frame model as the recompiled program. It
// runs the foreground from reset to a wait loop's transfer back to its start
// (the frame ends there), then each frame the interrupt handler and the
// foreground again to the next such transfer. tests/check_fixtures.py and
// `tests/check_codegen.py --metroid` compare its per-frame state hashes with
// the recompiled program's (codegen/replay.c); the wait loops come from the
// recompiler.
//
// usage: ./interpreter ROM.nes --waits [BANK:]FROM:TO,... [--rec FILE]
//                     [--frames N] [--write-trace FILE] [--dump N,...]
//                     [--profile FILE] [--poke F:ADDR=V,...]
//   a wait in a bank names it (the bank must be mapped there);
//   --poke writes RAM before frame F (hex address and value), as replay's;
//   --dump writes those frames as frame_N.ppm;
//   --profile writes what ran, for writing a map: the instructions run
//   (by bank), calls and jumps into a window from outside it with the bank
//   mapped, and the largest index of each jump table after a dispatcher.
//
// Like the recompiled program it calls the interrupt with the return address
// zero and the status as the hardware pushes it, and returns from it to
// where the foreground was.
#include "cpu.h"
#include "video.h"
#include "sound.h"
#include "cartridge.h"
#include "rec.h"
#include "mapper.h"
#include "state_hash.h"
#include "render.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void prg_load(const uint8_t *bytes, unsigned size); // prg.c

// cpu.c's next_frame calls these; the interpreter runs frames itself.
void cpu_reset_entry(void) {}
void cpu_nmi_entry(void) {}
void cpu_irq_entry(void) {}

static uint16_t pc;
static bool left_enabled; // the interrupt handler enabled interrupts (as cpu.c's cpu_interrupt)
#define MAX_WAITS 64
static uint16_t wait_from[MAX_WAITS], wait_to[MAX_WAITS];
static int wait_bank[MAX_WAITS]; // -1: a fixed bank
static int wait_count;

// The bank mapped at an address of PRG ROM (8 KiB banks).
static int bank_at(uint16_t addr) {
    return addr >= 0x8000 ? (int)(prg_slots[(addr >> 13) & 3] / 0x2000) : -1;
}

// --profile: what ran.
static FILE *profile;
static uint8_t ran[64][0x2000]; // by 8 KiB bank and offset
#define MAX_EDGES 8192
typedef struct { uint16_t site; int site_bank; uint16_t target; int bank; } Edge;
static Edge edges[MAX_EDGES];
static int edge_count;
typedef struct { uint16_t site; int bank; int max; } DispatchSite;
static DispatchSite dispatches[1024];
static int dispatch_count;
static uint16_t dispatcher = 0; // --dispatcher: the jump-table helper

static void note_edge(uint16_t site, uint16_t target) {
    int site_bank = bank_at(site), bank = bank_at(target);
    for (int i = 0; i < edge_count; i++) {
        if (edges[i].site == site && edges[i].site_bank == site_bank && edges[i].target == target && edges[i].bank == bank) return;
    }
    if (edge_count < MAX_EDGES) edges[edge_count++] = (Edge){ site, site_bank, target, bank };
}

static void note_dispatch(uint16_t site, int index) {
    int bank = bank_at(site);
    for (int i = 0; i < dispatch_count; i++) {
        if (dispatches[i].site == site && dispatches[i].bank == bank) {
            if (index > dispatches[i].max) dispatches[i].max = index;
            return;
        }
    }
    if (dispatch_count < 1024) dispatches[dispatch_count++] = (DispatchSite){ site, bank, index };
}

static uint8_t rd(uint16_t addr) { return read_byte(addr); }
static void wr(uint16_t addr, uint8_t v) { dynamic_ram_write(addr, v); }
static void push(uint8_t v) { ram[0x100 + sp--] = v; }
static uint8_t pull(void) { return ram[0x100 + ++sp]; }
static void nz(uint8_t v) { zero_flag = v == 0; neg_flag = (v & 0x80) != 0; }

static uint8_t status(uint8_t base) {
    return (uint8_t)(base | carry_flag | (zero_flag << 1) | (interrupt_disabled << 2) |
        (decimal_flag << 3) | (overflow_flag << 6) | (neg_flag << 7));
}

static void set_status(uint8_t p) {
    carry_flag = p & 1; zero_flag = (p >> 1) & 1; interrupt_disabled = (p >> 2) & 1;
    decimal_flag = (p >> 3) & 1; overflow_flag = (p >> 6) & 1; neg_flag = (p >> 7) & 1;
}

static uint16_t word_at(uint16_t addr) { return (uint16_t)(rd(addr) | (rd((uint16_t)(addr + 1)) << 8)); }

// Effective addresses by addressing mode.
enum { IMP, ACC, IMM, ZP, ZPX, ZPY, ABS, ABX, ABY, IND, IZX, IZY, REL };

static uint16_t address(int mode) {
    uint16_t a;
    switch (mode) {
        case ZP: return rd(pc++);
        case ZPX: return (uint8_t)(rd(pc++) + x);
        case ZPY: return (uint8_t)(rd(pc++) + y);
        case ABS: a = word_at(pc); pc += 2; return a;
        case ABX: a = word_at(pc); pc += 2; return (uint16_t)(a + x);
        case ABY: a = word_at(pc); pc += 2; return (uint16_t)(a + y);
        case IZX: { uint8_t p = (uint8_t)(rd(pc++) + x); return (uint16_t)(rd(p) | (rd((uint8_t)(p + 1)) << 8)); }
        case IZY: { uint8_t p = rd(pc++); return (uint16_t)((rd(p) | (rd((uint8_t)(p + 1)) << 8)) + y); }
        case IND: {
            a = word_at(pc); pc += 2;
            uint16_t high = (uint16_t)((a & 0xff00) | ((a + 1) & 0xff));
            return (uint16_t)(rd(a) | (rd(high) << 8));
        }
        default: return pc++; // IMM
    }
}

typedef struct { const char *name; int mode; } Op;
static Op ops[256];

static void define(int code, const char *name, int mode) { ops[code] = (Op){ name, mode }; }

static void init_ops(void) {
    static const struct { int code; const char *name; int mode; } table[] = {
        {0x69,"ADC",IMM},{0x65,"ADC",ZP},{0x75,"ADC",ZPX},{0x6d,"ADC",ABS},{0x7d,"ADC",ABX},{0x79,"ADC",ABY},{0x61,"ADC",IZX},{0x71,"ADC",IZY},
        {0x29,"AND",IMM},{0x25,"AND",ZP},{0x35,"AND",ZPX},{0x2d,"AND",ABS},{0x3d,"AND",ABX},{0x39,"AND",ABY},{0x21,"AND",IZX},{0x31,"AND",IZY},
        {0x0a,"ASL",ACC},{0x06,"ASL",ZP},{0x16,"ASL",ZPX},{0x0e,"ASL",ABS},{0x1e,"ASL",ABX},
        {0x90,"BCC",REL},{0xb0,"BCS",REL},{0xf0,"BEQ",REL},{0x30,"BMI",REL},{0xd0,"BNE",REL},{0x10,"BPL",REL},{0x50,"BVC",REL},{0x70,"BVS",REL},
        {0x24,"BIT",ZP},{0x2c,"BIT",ABS},
        {0x18,"CLC",IMP},{0xd8,"CLD",IMP},{0x58,"CLI",IMP},{0xb8,"CLV",IMP},
        {0xc9,"CMP",IMM},{0xc5,"CMP",ZP},{0xd5,"CMP",ZPX},{0xcd,"CMP",ABS},{0xdd,"CMP",ABX},{0xd9,"CMP",ABY},{0xc1,"CMP",IZX},{0xd1,"CMP",IZY},
        {0xe0,"CPX",IMM},{0xe4,"CPX",ZP},{0xec,"CPX",ABS},{0xc0,"CPY",IMM},{0xc4,"CPY",ZP},{0xcc,"CPY",ABS},
        {0xc6,"DEC",ZP},{0xd6,"DEC",ZPX},{0xce,"DEC",ABS},{0xde,"DEC",ABX},{0xca,"DEX",IMP},{0x88,"DEY",IMP},
        {0x49,"EOR",IMM},{0x45,"EOR",ZP},{0x55,"EOR",ZPX},{0x4d,"EOR",ABS},{0x5d,"EOR",ABX},{0x59,"EOR",ABY},{0x41,"EOR",IZX},{0x51,"EOR",IZY},
        {0xe6,"INC",ZP},{0xf6,"INC",ZPX},{0xee,"INC",ABS},{0xfe,"INC",ABX},{0xe8,"INX",IMP},{0xc8,"INY",IMP},
        {0x4c,"JMP",ABS},{0x6c,"JMP",IND},{0x20,"JSR",ABS},
        {0xa9,"LDA",IMM},{0xa5,"LDA",ZP},{0xb5,"LDA",ZPX},{0xad,"LDA",ABS},{0xbd,"LDA",ABX},{0xb9,"LDA",ABY},{0xa1,"LDA",IZX},{0xb1,"LDA",IZY},
        {0xa2,"LDX",IMM},{0xa6,"LDX",ZP},{0xb6,"LDX",ZPY},{0xae,"LDX",ABS},{0xbe,"LDX",ABY},
        {0xa0,"LDY",IMM},{0xa4,"LDY",ZP},{0xb4,"LDY",ZPX},{0xac,"LDY",ABS},{0xbc,"LDY",ABX},
        {0x4a,"LSR",ACC},{0x46,"LSR",ZP},{0x56,"LSR",ZPX},{0x4e,"LSR",ABS},{0x5e,"LSR",ABX},
        {0xea,"NOP",IMP},
        {0x09,"ORA",IMM},{0x05,"ORA",ZP},{0x15,"ORA",ZPX},{0x0d,"ORA",ABS},{0x1d,"ORA",ABX},{0x19,"ORA",ABY},{0x01,"ORA",IZX},{0x11,"ORA",IZY},
        {0x48,"PHA",IMP},{0x08,"PHP",IMP},{0x68,"PLA",IMP},{0x28,"PLP",IMP},
        {0x2a,"ROL",ACC},{0x26,"ROL",ZP},{0x36,"ROL",ZPX},{0x2e,"ROL",ABS},{0x3e,"ROL",ABX},
        {0x6a,"ROR",ACC},{0x66,"ROR",ZP},{0x76,"ROR",ZPX},{0x6e,"ROR",ABS},{0x7e,"ROR",ABX},
        {0x40,"RTI",IMP},{0x60,"RTS",IMP},
        {0xe9,"SBC",IMM},{0xe5,"SBC",ZP},{0xf5,"SBC",ZPX},{0xed,"SBC",ABS},{0xfd,"SBC",ABX},{0xf9,"SBC",ABY},{0xe1,"SBC",IZX},{0xf1,"SBC",IZY},
        {0x38,"SEC",IMP},{0xf8,"SED",IMP},{0x78,"SEI",IMP},
        {0x85,"STA",ZP},{0x95,"STA",ZPX},{0x8d,"STA",ABS},{0x9d,"STA",ABX},{0x99,"STA",ABY},{0x81,"STA",IZX},{0x91,"STA",IZY},
        {0x86,"STX",ZP},{0x96,"STX",ZPY},{0x8e,"STX",ABS},{0x84,"STY",ZP},{0x94,"STY",ZPX},{0x8c,"STY",ABS},
        {0xaa,"TAX",IMP},{0xa8,"TAY",IMP},{0xba,"TSX",IMP},{0x8a,"TXA",IMP},{0x9a,"TXS",IMP},{0x98,"TYA",IMP},
    };
    for (size_t i = 0; i < sizeof(table) / sizeof(table[0]); i++) define(table[i].code, table[i].name, table[i].mode);
}

static void adc(uint8_t m) {
    unsigned sum = a + m + carry_flag;
    overflow_flag = (~(a ^ m) & (a ^ sum) & 0x80) != 0;
    carry_flag = sum > 0xff;
    a = (uint8_t)sum;
    nz(a);
}

static void compare(uint8_t r, uint8_t m) {
    carry_flag = r >= m;
    nz((uint8_t)(r - m));
}

// Read-modify-write: the old value, then the new.
static uint8_t modify(int mode, uint16_t addr, uint8_t (*f)(uint8_t)) {
    if (mode == ACC) { a = f(a); return a; }
    uint8_t v = f(rd(addr));
    wr(addr, v);
    return v;
}
static uint8_t asl(uint8_t v) { carry_flag = v >> 7; return (uint8_t)(v << 1); }
static uint8_t lsr(uint8_t v) { carry_flag = v & 1; return v >> 1; }
static uint8_t rol(uint8_t v) { uint8_t c = carry_flag; carry_flag = v >> 7; return (uint8_t)((v << 1) | c); }
static uint8_t ror(uint8_t v) { uint8_t c = carry_flag; carry_flag = v & 1; return (uint8_t)((v >> 1) | (c << 7)); }
static uint8_t inc(uint8_t v) { return (uint8_t)(v + 1); }
static uint8_t dec(uint8_t v) { return (uint8_t)(v - 1); }

// One instruction. Returns false at an RTI (the end of the interrupt).
static bool step(void) {
    uint16_t at = pc;
    if (profile && at >= 0x8000) ran[bank_at(at)][at & 0x1fff] = 1;
    uint8_t code = rd(pc++);
    Op op = ops[code];
    if (!op.name) { fprintf(stderr, "Unsupported opcode %02x at %04x\n", code, at); exit(2); }
    const char *n = op.name;
    if (op.mode == REL) {
        int8_t offset = (int8_t)rd(pc++);
        bool taken;
        switch (code) {
            case 0x90: taken = !carry_flag; break; case 0xb0: taken = carry_flag; break;
            case 0xf0: taken = zero_flag; break; case 0xd0: taken = !zero_flag; break;
            case 0x30: taken = neg_flag; break; case 0x10: taken = !neg_flag; break;
            case 0x50: taken = !overflow_flag; break; default: taken = overflow_flag; break;
        }
        if (taken) pc = (uint16_t)(pc + offset);
        return true;
    }
    if (!strcmp(n, "JSR")) {
        uint16_t target = word_at(pc);
        uint16_t ret = (uint16_t)(pc + 1);
        push((uint8_t)(ret >> 8)); push((uint8_t)ret);
        if (profile && (target >> 13) != (at >> 13) && target >= 0x8000) note_edge(at, target);
        if (profile && dispatcher && target == dispatcher) note_dispatch(at, a);
        pc = target;
        return true;
    }
    if (!strcmp(n, "RTS")) { uint16_t lo = pull(); uint16_t hi = pull(); pc = (uint16_t)(((hi << 8) | lo) + 1); return true; }
    if (!strcmp(n, "RTI")) { if (!interrupt_disabled) left_enabled = true; set_status(pull()); pull(); pull(); return false; }
    if (!strcmp(n, "JMP")) {
        pc = address(op.mode);
        if (profile && (pc >> 13) != (at >> 13) && pc >= 0x8000) note_edge(at, pc);
        return true;
    }
    uint16_t addr = (op.mode == IMP || op.mode == ACC) ? 0 : address(op.mode);
    #define M (op.mode == IMM ? rd(addr) : rd(addr))
    if (!strcmp(n, "LDA")) { a = M; nz(a); }
    else if (!strcmp(n, "LDX")) { x = M; nz(x); }
    else if (!strcmp(n, "LDY")) { y = M; nz(y); }
    else if (!strcmp(n, "STA")) wr(addr, a);
    else if (!strcmp(n, "STX")) wr(addr, x);
    else if (!strcmp(n, "STY")) wr(addr, y);
    else if (!strcmp(n, "ADC")) adc(M);
    else if (!strcmp(n, "SBC")) adc((uint8_t)~M);
    else if (!strcmp(n, "AND")) { a &= M; nz(a); }
    else if (!strcmp(n, "ORA")) { a |= M; nz(a); }
    else if (!strcmp(n, "EOR")) { a ^= M; nz(a); }
    else if (!strcmp(n, "CMP")) compare(a, M);
    else if (!strcmp(n, "CPX")) compare(x, M);
    else if (!strcmp(n, "CPY")) compare(y, M);
    else if (!strcmp(n, "BIT")) { uint8_t m = M; zero_flag = (a & m) == 0; neg_flag = m >> 7; overflow_flag = (m >> 6) & 1; }
    else if (!strcmp(n, "ASL")) nz(modify(op.mode, addr, asl));
    else if (!strcmp(n, "LSR")) nz(modify(op.mode, addr, lsr));
    else if (!strcmp(n, "ROL")) nz(modify(op.mode, addr, rol));
    else if (!strcmp(n, "ROR")) nz(modify(op.mode, addr, ror));
    else if (!strcmp(n, "INC")) nz(modify(op.mode, addr, inc));
    else if (!strcmp(n, "DEC")) nz(modify(op.mode, addr, dec));
    else if (!strcmp(n, "INX")) nz(++x);
    else if (!strcmp(n, "INY")) nz(++y);
    else if (!strcmp(n, "DEX")) nz(--x);
    else if (!strcmp(n, "DEY")) nz(--y);
    else if (!strcmp(n, "TAX")) nz(x = a);
    else if (!strcmp(n, "TAY")) nz(y = a);
    else if (!strcmp(n, "TXA")) nz(a = x);
    else if (!strcmp(n, "TYA")) nz(a = y);
    else if (!strcmp(n, "TSX")) nz(x = sp);
    else if (!strcmp(n, "TXS")) sp = x;
    else if (!strcmp(n, "PHA")) push(a);
    else if (!strcmp(n, "PHP")) push(status(0x30));
    else if (!strcmp(n, "PLA")) nz(a = pull());
    else if (!strcmp(n, "PLP")) set_status(pull());
    else if (!strcmp(n, "CLC")) carry_flag = 0;
    else if (!strcmp(n, "SEC")) carry_flag = 1;
    else if (!strcmp(n, "CLI")) { interrupt_disabled = 0; left_enabled = true; }
    else if (!strcmp(n, "SEI")) interrupt_disabled = 1;
    else if (!strcmp(n, "CLD")) decimal_flag = 0;
    else if (!strcmp(n, "SED")) decimal_flag = 1;
    else if (!strcmp(n, "CLV")) overflow_flag = 0;
    return true;
}

// Runs the foreground to the next transfer back to a wait loop's start.
static void run_foreground(void) {
    for (long count = 0;; count++) {
        uint16_t at = pc;
        step();
        for (int i = 0; i < wait_count; i++) {
            if (wait_from[i] == at && wait_to[i] == pc && (wait_bank[i] < 0 || wait_bank[i] == bank_at(at))) return;
        }
        if (count > 50000000) { fprintf(stderr, "The foreground does not reach a wait (at %04x)\n", pc); exit(3); }
    }
}

// An interrupt through `vector`, as next_frame and the generated prologue
// deliver it.
static void interrupt_through(uint16_t vector) {
    bool c = carry_flag, z = zero_flag, n = neg_flag, v = overflow_flag, i = interrupt_disabled, d = decimal_flag;
    uint16_t resume = pc;
    interrupt_disabled = 1;
    left_enabled = false;
    push(0); push(0);
    push(status(0x20));
    pc = word_at(vector);
    while (step()) {}
    carry_flag = c; zero_flag = z; neg_flag = n; overflow_flag = v; interrupt_disabled = i; decimal_flag = d;
    pc = resume;
}

static void interrupt(void) { interrupt_through(0xfffa); }
static void irq(void) { interrupt_through(0xfffe); }

// Whether frame `i` is in the comma-separated list.
static bool dumps(const char *list, uint32_t i) {
    for (const char *p = list; *p;) {
        if (strtoul(p, NULL, 10) == i) return true;
        p = strchr(p, ',');
        if (!p) break;
        p++;
    }
    return false;
}

static void write_ppm(const char *path) {
    FILE *file = fopen(path, "wb");
    if (!file) return;
    fprintf(file, "P6\n%d %d\n255\n", SCREEN_WIDTH, SCREEN_HEIGHT);
    fwrite(frame, 1, FRAME_BUFFER_SIZE, file);
    fclose(file);
}

// RAM, or the cartridge's (--poke)
static void poke(uint16_t addr, uint8_t value) {
#ifdef MAPPER
    if (addr >= 0x6000 && addr < 0x8000) { wram[addr - 0x6000] = value; return; }
#endif
    ram[addr & 0x7ff] = value;
}

static const char *option(int argc, char **argv, const char *name) {
    for (int i = 1; i + 1 < argc; i++) {
        if (strcmp(argv[i], name) == 0) return argv[i + 1];
    }
    return NULL;
}

int main(int argc, char **argv) {
    const char *waits = option(argc, argv, "--waits");
    const char *rec_path = option(argc, argv, "--rec");
    const char *frames_option = option(argc, argv, "--frames");
    const char *write_trace_path = option(argc, argv, "--write-trace");
    const char *dump = option(argc, argv, "--dump");
    const char *profile_path = option(argc, argv, "--profile");
    const char *dispatcher_option = option(argc, argv, "--dispatcher");
    const char *pokes = option(argc, argv, "--poke");
    if (argc < 2 || !waits) {
        fprintf(stderr, "usage: %s ROM.nes --waits [BANK:]FROM:TO,... [--rec FILE] [--frames N] [--write-trace FILE] [--dump N,...] [--profile FILE] [--poke F:ADDR=V,...]\n", argv[0]);
        return 1;
    }
    for (const char *p = waits; *p && wait_count < MAX_WAITS;) {
        unsigned bank, from, to;
        if (sscanf(p, "%u:%x:%x", &bank, &from, &to) == 3) {
            wait_bank[wait_count] = (int)bank;
        } else if (sscanf(p, "%x:%x", &from, &to) == 2) {
            wait_bank[wait_count] = -1;
        } else {
            break;
        }
        wait_from[wait_count] = (uint16_t)from;
        wait_to[wait_count++] = (uint16_t)to;
        p = strchr(p, ',');
        if (!p) break;
        p++;
    }
    if (profile_path) profile = fopen(profile_path, "w");
    if (dispatcher_option) dispatcher = (uint16_t)strtoul(dispatcher_option, NULL, 16);
    init_ops();

    // the PRG ROM from the file, into the runtime's buffer
    FILE *file = fopen(argv[1], "rb");
    uint8_t header[16];
    if (!file || fread(header, 1, 16, file) != 16) { fprintf(stderr, "Cannot read %s\n", argv[1]); return 1; }
    unsigned prg_size = header[4] * 0x4000u;
    static uint8_t prg[0x40000];
    fseek(file, 16 + ((header[6] & 4) ? 512 : 0), SEEK_SET);
    if (prg_size > sizeof(prg) || fread(prg, 1, prg_size, file) != prg_size) { fprintf(stderr, "Truncated %s\n", argv[1]); return 1; }
    fclose(file);
    prg_load(prg, prg_size);

    cpu_init();
    apu_init(48000);
    if (load_cartridge(argv[1])) return 1;
    mapper_init(cartridge_mapper);

    NESInputRecording recording = {0};
    if (rec_path && rec_open(rec_path, "r", &recording) != 0) { fprintf(stderr, "Cannot open %s\n", rec_path); return 1; }
    uint32_t frames = frames_option ? (uint32_t)strtoul(frames_option, NULL, 10)
                    : rec_path ? rec_length(rec_path) : 600;
    FILE *trace = write_trace_path ? fopen(write_trace_path, "w") : NULL;

    pc = word_at(0xfffc);
    run_foreground();
    for (uint32_t i = 0; i < frames; i++) {
        update_controller1(rec_path ? rec_state_at_frame(&recording, i) : 0);
        rec_pokes(pokes, i, poke);
        interrupt();
        mapper_frame(irq, left_enabled || !interrupt_disabled);
        run_foreground();
        sound_end_frame();
        if (trace) fprintf(trace, "%08x\n", hash_state());
        if (dump && dumps(dump, i)) {
            if (option(argc, argv, "--splits") || (argc > 0 && strcmp(argv[argc - 1], "--splits") == 0)) {
                printf("frame %u: %u splits, irq latch %u enabled %u, mask %02x\n", i, video.split_count, mmc3.irq_latch, mmc3.irq_enabled, video.mask);
                for (int k = 0; k < video.split_count; k++) {
                    const VideoSplit *sp = &video.splits[k];
                    printf("  line %u ctrl %02x mask %02x x %u y %u banks %u %u %u %u %u %u %u %u\n", sp->line, sp->ctrl, sp->mask, sp->x, sp->y,
                           sp->chr_banks[0], sp->chr_banks[1], sp->chr_banks[2], sp->chr_banks[3], sp->chr_banks[4], sp->chr_banks[5], sp->chr_banks[6], sp->chr_banks[7]);
                }
            }
            render_frame();
            char name[64];
            snprintf(name, sizeof name, "frame_%u.ppm", i);
            write_ppm(name);
        }
    }
    if (trace) fclose(trace);
    if (profile) {
        for (int bank = 0; bank < 64; bank++) {
            for (int offset = 0; offset < 0x2000; offset++) {
                if (ran[bank][offset]) fprintf(profile, "ran %d %04x\n", bank, offset);
            }
        }
        for (int i = 0; i < edge_count; i++) {
            fprintf(profile, "edge %d %04x %d %04x\n", edges[i].site_bank, edges[i].site, edges[i].bank, edges[i].target);
        }
        for (int i = 0; i < dispatch_count; i++) {
            fprintf(profile, "dispatch %d %04x %d\n", dispatches[i].bank, dispatches[i].site, dispatches[i].max);
        }
        fclose(profile);
    }
    printf("Interpreted %u frames\n", frames);
    return 0;
}
