#include "mapper.h"
#include "cpu.h"
#include "video.h"

uint8_t wram[WRAM_SIZE];
uint32_t prg_slots[4];
uint8_t prg_bank, prg_bank1;
uint8_t mapper_number;
Mmc1 mmc1;
Mmc3 mmc3;

static void mmc1_apply(void) {
    // Code was recompiled for the last bank at $C000 and one switched in at
    // $8000: other PRG modes would run code the recompiler did not see.
    if ((mmc1.control & 0x0c) != 0x0c) __builtin_trap();
    unsigned banks = prg_image_size / 0x4000;
    prg_bank = (uint8_t)((mmc1.prg & 0x0f) & (banks - 1));
    prg_bank1 = 0;
    prg_slots[0] = prg_bank * 0x4000u;
    prg_slots[1] = prg_bank * 0x4000u + 0x2000;
    prg_slots[2] = (banks - 1) * 0x4000u;
    prg_slots[3] = (banks - 1) * 0x4000u + 0x2000;
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
    if (mapper_number == 4) mmc3_apply(); else mmc1_apply();
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
        case 1: mmc1.chr0 = mmc1.shift; break; // CHR RAM: 8 KiB, not banked here
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
    if (mapper_number == 4) mmc3_write(addr, value); else mmc1_write(addr, value);
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
