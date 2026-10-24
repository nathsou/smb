#include "lib/cpu.h"
#include "lib/video.h"
#include "lib/render.h"
#include "lib/sound.h"
#include "lib/cartridge.h"
#include "lib/rec.h"
#include "lib/ppu_bus.h"
#ifdef MAPPER
#include "lib/mapper.h"
#endif
#include <stdlib.h>
#include <string.h>

// Replays an input recording headlessly for programs that drive the PPU
// through its registers (Metroid), and hashes the state after each frame:
// RAM, the cartridge's RAM, the pattern tables, everything the renderer
// draws from, and the mapper.
//
// usage: ./replay ROM.nes [--rec FILE] [--frames N] [--trace FILE]
//                 [--write-trace FILE] [--dump N[,N...]]
//   --rec FILE          joypad 1 states by frame ("frame:state" lines)
//   --frames N          frames to run (default: to the recording's last)
//   --trace FILE        compare per-frame state hashes, report the first
//                       divergent frame
//   --write-trace FILE  record per-frame state hashes
//   --dump N[,N...]     write those frames as frame_N.ppm
//   --dump-every N      write every Nth frame

#define HASH_OFFSET_BASIS 2166136261u
#define HASH_PRIME 16777619u

static uint32_t hash_bytes(uint32_t hash, const uint8_t *data, size_t size) {
    for (size_t i = 0; i < size; i++) {
        hash ^= data[i];
        hash *= HASH_PRIME;
    }
    return hash;
}

static uint32_t hash_byte(uint32_t hash, uint8_t value) {
    return hash_bytes(hash, &value, 1);
}

uint32_t hash_state(void) {
    uint32_t hash = HASH_OFFSET_BASIS;
    hash = hash_bytes(hash, ram, RAM_SIZE);
#ifdef MAPPER
    hash = hash_bytes(hash, wram, WRAM_SIZE);
    hash = hash_byte(hash, prg_bank);
#endif
    hash = hash_bytes(hash, chr_rom, CHR_ROM_SIZE);
    hash = hash_bytes(hash, video.nametable, NAMETABLE_SIZE);
    hash = hash_bytes(hash, video.palette.u8, PALETTE_SIZE);
    hash = hash_bytes(hash, video.oam, OAM_SIZE);
    hash = hash_byte(hash, video.ctrl);
    hash = hash_byte(hash, video.mask);
    hash = hash_byte(hash, video.scroll_x);
    hash = hash_byte(hash, video.scroll_y);
    hash = hash_byte(hash, video.mirroring);
    return hash;
}

static void dump_frame(uint32_t index) {
    char path[32];
    snprintf(path, sizeof(path), "frame_%u.ppm", index);
    FILE *file = fopen(path, "wb");
    if (!file) return;
    fprintf(file, "P6\n%d %d\n255\n", SCREEN_WIDTH, SCREEN_HEIGHT);
    fwrite(frame, 1, SCREEN_WIDTH * SCREEN_HEIGHT * 3, file);
    fclose(file);
}

static bool listed(const char *list, uint32_t index) {
    while (list && *list) {
        char *end;
        unsigned long value = strtoul(list, &end, 10);
        if (end == list) return false;
        if (value == index) return true;
        list = *end == ',' ? end + 1 : end;
    }
    return false;
}

static const char *option(int argc, char **argv, const char *name) {
    for (int i = 1; i + 1 < argc; i++) {
        if (strcmp(argv[i], name) == 0) return argv[i + 1];
    }
    return NULL;
}

// The last frame a recording names.
static uint32_t recording_length(const char *path) {
    FILE *file = fopen(path, "r");
    uint32_t frame = 0, last = 0;
    uint8_t state;
    while (file && fscanf(file, "%u:%hhu\n", &frame, &state) == 2) last = frame;
    if (file) fclose(file);
    return last + 1;
}

int main(int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr, "usage: %s ROM.nes [--rec FILE] [--frames N] [--trace FILE] [--write-trace FILE] [--dump N,...]\n", argv[0]);
        return 1;
    }
    const char *rec_path = option(argc, argv, "--rec");
    const char *frames_option = option(argc, argv, "--frames");
    const char *trace_path = option(argc, argv, "--trace");
    const char *write_trace_path = option(argc, argv, "--write-trace");
    const char *dump = option(argc, argv, "--dump");
    const char *every_option = option(argc, argv, "--dump-every");
    uint32_t every = every_option ? (uint32_t)strtoul(every_option, NULL, 10) : 0;

    cpu_init();
    apu_init(48000);
    if (load_cartridge(argv[1])) return 1;
#ifdef MAPPER
    mapper_init();
#endif

    NESInputRecording recording = {0};
    if (rec_path && rec_open(rec_path, "r", &recording) != 0) {
        fprintf(stderr, "Error: Could not open %s\n", rec_path);
        return 1;
    }
    uint32_t frames = frames_option ? (uint32_t)strtoul(frames_option, NULL, 10)
                    : rec_path ? recording_length(rec_path) : 600;

    FILE *trace = trace_path ? fopen(trace_path, "r") : NULL;
    FILE *write_trace = write_trace_path ? fopen(write_trace_path, "w") : NULL;
    if ((trace_path && !trace) || (write_trace_path && !write_trace)) {
        fprintf(stderr, "Error: Could not open trace file\n");
        return 1;
    }

    cpu_reset_entry();
    long first_divergence = -1;
    for (uint32_t i = 0; i < frames; i++) {
        update_controller1(rec_path ? rec_state_at_frame(&recording, i) : 0);
        next_frame();
        sound_end_frame();
        uint32_t state = hash_state();
        if (write_trace) fprintf(write_trace, "%08x\n", state);
        if (trace && first_divergence < 0) {
            unsigned expected;
            if (fscanf(trace, "%x", &expected) != 1 || expected != state) first_divergence = (long)i;
        }
        if (listed(dump, i) || (every && i % every == 0)) {
            render_frame();
            dump_frame(i);
        }
    }
    if (rec_path) rec_close(&recording);
    if (write_trace) fclose(write_trace);
    if (trace) {
        fclose(trace);
        if (first_divergence >= 0) {
            printf("State diverges from %s at frame %ld\n", trace_path, first_divergence);
            return 1;
        }
        printf("Per-frame state matches %s\n", trace_path);
    }
    printf("Ran %u frames\n", frames);
    return 0;
}
