#include "mapper.h"
#include "cpu.h"
#include "video.h"
#include "ppu_bus.h"
#include "external.h"

uint8_t wram[WRAM_SIZE];
uint32_t prg_slots[4];
uint8_t prg_bank, prg_bank1;
uint8_t mapper_number;
uint8_t mapper_header_mirroring;
Mmc1 mmc1;
Mmc3 mmc3;

// NROM: no registers, 16 KiB of PRG ROM seen twice or 32 KiB, the
// mirroring wired on the board.
static void nrom_apply(void) {
    for (int i = 0; i < 4; i++) prg_slots[i] = (uint32_t)(i * 0x2000u) % prg_image_size;
    prg_bank = prg_bank1 = 0;
    video.mirroring = mapper_header_mirroring;
}

static void mmc1_apply(void) {
    unsigned banks = prg_image_size / 0x4000;
    unsigned mode = (mmc1.control >> 2) & 3;
    unsigned low, high;
    if (mode < 2) {
        // 32 KiB at $8000 (the low bit of the bank ignored)
        low = (mmc1.prg & 0x0e) & (banks - 1);
        high = low + 1;
    } else if (mode == 2) {
        low = 0; // the first bank fixed at $8000
        high = (mmc1.prg & 0x0f) & (banks - 1);
    } else {
        low = (mmc1.prg & 0x0f) & (banks - 1);
        high = banks - 1; // the last bank fixed at $C000
    }
    // Code was recompiled for the last bank at $C000 and one switched in at
    // $8000: a mode that puts another bank there would run code the
    // recompiler did not see (32 KiB of PRG ROM is the same in every mode).
    // (with 32 KiB the code was recompiled as one image: the first bank at
    // $8000)
    if (high != banks - 1 || (banks == 2 && low != 0)) __builtin_trap();
    prg_bank = (uint8_t)low;
    prg_bank1 = 0;
    prg_slots[0] = low * 0x4000u;
    prg_slots[1] = low * 0x4000u + 0x2000;
    prg_slots[2] = high * 0x4000u;
    prg_slots[3] = high * 0x4000u + 0x2000;
    // CHR ROM: two 4 KiB banks, or one of 8 KiB (CHR RAM has 8 KiB, not
    // banked)
    if (!chr_ram) {
        unsigned mask = video_chr_banks() - 1;
        unsigned first, second;
        if (mmc1.control & 0x10) {
            first = mmc1.chr0 * 4u;
            second = mmc1.chr1 * 4u;
        } else {
            first = (mmc1.chr0 & 0x1e) * 4u;
            second = first + 4;
        }
        for (unsigned i = 0; i < 4; i++) {
            video.chr_banks[i] = (uint8_t)((first + i) & mask);
            video.chr_banks[4 + i] = (uint8_t)((second + i) & mask);
        }
    }
    // one screen (lower, upper), vertical, horizontal
    static const uint8_t mirroring[4] = {
        MIRROR_SINGLE_LOWER, MIRROR_SINGLE_UPPER, MIRROR_VERTICAL, MIRROR_HORIZONTAL,
    };
    video.mirroring = mirroring[mmc1.control & 3];
}

static void mmc3_apply(void) {
    unsigned banks = prg_image_size / 0x2000;
    bool mode1 = (mmc3.select & 0x40) != 0;
    if ((mmc3.prg_mode & 0x80) && mode1 != ((mmc3.prg_mode & 1) != 0)) __builtin_trap();
    uint8_t r6 = (uint8_t)(mmc3.banks[6] & (banks - 1));
    uint8_t r7 = (uint8_t)(mmc3.banks[7] & (banks - 1));
    uint8_t second_last = (uint8_t)(banks - 2);
    uint8_t slots[4] = { mode1 ? second_last : r6, r7, mode1 ? r6 : second_last, (uint8_t)(banks - 1) };
    for (int i = 0; i < 4; i++) prg_slots[i] = slots[i] * 0x2000u;
    // the windows in address order: $8000 and $A000, or $A000 and $C000
    prg_bank = mode1 ? r7 : r6;
    prg_bank1 = mode1 ? r6 : r7;
    // 2 KiB banks R0 and R1, 1 KiB banks R2-R5; inverted, the 1 KiB banks
    // come first
    unsigned mask = video_chr_banks() - 1;
    uint8_t chr[8] = {
        (uint8_t)(mmc3.banks[0] & 0xfe), (uint8_t)(mmc3.banks[0] | 1),
        (uint8_t)(mmc3.banks[1] & 0xfe), (uint8_t)(mmc3.banks[1] | 1),
        mmc3.banks[2], mmc3.banks[3], mmc3.banks[4], mmc3.banks[5],
    };
    int inverted = (mmc3.select & 0x80) ? 4 : 0;
    for (int i = 0; i < 8; i++) video.chr_banks[i ^ inverted] = (uint8_t)(chr[i] & mask);
    video.mirroring = (mmc3.mirroring & 1) ? MIRROR_HORIZONTAL : MIRROR_VERTICAL;
}

void mapper_apply(void) {
    if (mapper_number == 4) mmc3_apply();
    else if (mapper_number == 1) mmc1_apply();
    else nrom_apply();
}

void mapper_mirroring(uint8_t vertical) {
    mapper_header_mirroring = vertical ? MIRROR_VERTICAL : MIRROR_HORIZONTAL;
    mapper_apply();
}

void mapper_init(uint8_t mapper) {
    mapper_number = mapper;
    mmc1 = (Mmc1){ .control = 0x0c };
    mmc3 = (Mmc3){ 0 };
    // MMC3 powers on with its banks unknown; the game sets them at reset
    for (int i = 0; i < 8; i++) mmc3.banks[i] = (uint8_t)i;
    mapper_apply();
}

void mapper_prg_mode(uint8_t mode) {
    mmc3.prg_mode = (uint8_t)(0x80 | (mode & 1));
    mmc3.select = (uint8_t)((mmc3.select & ~0x40) | ((mode & 1) << 6));
    mapper_apply();
}

static void mmc1_write(uint16_t addr, uint8_t value) {
    if (value & 0x80) {
        // reset the shift register, fix the last bank at $C000
        mmc1.shift = 0;
        mmc1.count = 0;
        mmc1.control |= 0x0c;
        mmc1_apply();
        return;
    }
    mmc1.shift = (uint8_t)((mmc1.shift >> 1) | ((value & 1) << 4));
    if (++mmc1.count < 5) return;
    switch ((addr >> 13) & 3) {
        case 0: mmc1.control = mmc1.shift; break;
        case 1: mmc1.chr0 = mmc1.shift; break;
        case 2: mmc1.chr1 = mmc1.shift; break;
        case 3: mmc1.prg = mmc1.shift; break;
    }
    mmc1.shift = 0;
    mmc1.count = 0;
    mmc1_apply();
}

static void mmc3_write(uint16_t addr, uint8_t value) {
    bool odd = addr & 1;
    switch ((addr >> 13) & 3) {
        case 0:
            if (odd) mmc3.banks[mmc3.select & 7] = value; else mmc3.select = value;
            mmc3_apply();
            break;
        case 1:
            if (!odd) {
                mmc3.mirroring = value;
                mmc3_apply();
            } // odd: PRG RAM protection, left enabled
            break;
        case 2:
            if (odd) {
                mmc3.irq_counter = 0;
                mmc3.irq_reload = 1;
            } else {
                mmc3.irq_latch = value;
            }
            break;
        case 3:
            mmc3.irq_enabled = odd;
            if (!odd) mmc3.irq_pending = 0;
            break;
    }
}

void mapper_write(uint16_t addr, uint8_t value) {
    if (mapper_number == 4) mmc3_write(addr, value);
    else if (mapper_number == 1) mmc1_write(addr, value);
    // NROM has no registers: writes to ROM do nothing
}

bool mapper_scanline(void) {
    if (mapper_number != 4) return false;
    if (mmc3.irq_counter == 0 || mmc3.irq_reload) {
        mmc3.irq_counter = mmc3.irq_latch;
        mmc3.irq_reload = 0;
    } else {
        mmc3.irq_counter--;
    }
    if (mmc3.irq_counter == 0 && mmc3.irq_enabled && !mmc3.irq_pending) {
        mmc3.irq_pending = 1;
        return true;
    }
    return false;
}

static void split_at(uint8_t line, uint16_t x, uint16_t y) {
    if (video.split_count == VIDEO_MAX_SPLITS) video.split_count--; // the last one gives way
    VideoSplit *split = &video.splits[video.split_count++];
    split->line = line;
    split->ctrl = video.ctrl;
    split->mask = video.mask;
    memcpy(split->chr_banks, video.chr_banks, 8);
    split->pad = 0;
    split->x = (uint16_t)(x % 512);
    split->y = (uint16_t)(y % 480);
}

void mapper_frame(void (*irq)(void), bool enabled) {
    video.split_count = 0;
    if (mapper_number != 4) return;
    for (int line = -1; line < SCREEN_HEIGHT; line++) {
        if (!(video.mask & (VIDEO_MASK_BACKGROUND | VIDEO_MASK_SPRITES))) continue;
        if (!mapper_scanline() || !enabled) continue;
        if (video.split_count == 0) {
            // the frame's top, from the scroll and control the NMI left
            split_at(0, (uint16_t)(video.scroll_x + ((video.ctrl & 1) ? 256 : 0)),
                     (uint16_t)(video.scroll_y + ((video.ctrl & 2) ? 240 : 0)));
        }
        ppu_address_written = false;
        irq();
        int next = line + 1;
        if (next >= SCREEN_HEIGHT) continue;
        // the horizontal scroll is the temporary address's, from the next
        // scanline on; the frame continues vertically where it was, unless
        // $2006 set where
        uint16_t t = ppu_temp_addr, v = ppu_address_set;
        uint16_t x = (uint16_t)(((t >> 10) & 1) * 256 + (t & 0x1f) * 8 + ppu_fine_x);
        const VideoSplit *last = &video.splits[video.split_count - 1];
        uint16_t y = ppu_address_written
            ? (uint16_t)(((v >> 11) & 1) * 240 + ((v >> 5) & 0x1f) * 8 + ((v >> 12) & 7))
            : (uint16_t)(last->y + (next - last->line));
        if (last->line == next) video.split_count--; // replaced
        split_at((uint8_t)next, x, y);
    }
}
