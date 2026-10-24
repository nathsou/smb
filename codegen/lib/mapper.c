#include "mapper.h"
#include "cpu.h"
#include "video.h"

uint8_t wram[WRAM_SIZE];
uint8_t prg_bank;
const uint8_t *prg_window;
Mmc1 mmc1;

void mapper_apply(void) {
    // Code was recompiled for the last bank at $C000 and one switched in at
    // $8000: other PRG modes would run code the recompiler did not see.
    if ((mmc1.control & 0x0c) != 0x0c) __builtin_trap();
    unsigned banks = prg_image_size / 0x4000;
    prg_bank = (uint8_t)((mmc1.prg & 0x0f) & (banks - 1));
    prg_window = data + prg_bank * 0x4000;
    // one screen (lower, upper), vertical, horizontal
    static const uint8_t mirroring[4] = {
        MIRROR_SINGLE_LOWER, MIRROR_SINGLE_UPPER, MIRROR_VERTICAL, MIRROR_HORIZONTAL,
    };
    video.mirroring = mirroring[mmc1.control & 3];
}

void mapper_init(void) {
    mmc1 = (Mmc1){ .control = 0x0c };
    mapper_apply();
}

void mapper_write(uint16_t addr, uint8_t value) {
    if (value & 0x80) {
        // reset the shift register, fix the last bank at $C000
        mmc1.shift = 0;
        mmc1.count = 0;
        mmc1.control |= 0x0c;
        mapper_apply();
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
    mapper_apply();
}
