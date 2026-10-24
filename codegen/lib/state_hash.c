#include "state_hash.h"
#include "cpu.h"
#include "video.h"
#ifdef MAPPER
#include "mapper.h"
#endif

#define HASH_OFFSET_BASIS 2166136261u
#define HASH_PRIME 16777619u

static uint32_t hash_bytes(uint32_t hash, const uint8_t *bytes, unsigned size) {
    for (unsigned i = 0; i < size; i++) {
        hash ^= bytes[i];
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
    if (mapper_number == 4) {
        // MMC3: the second window, the CHR banks, the frame's splits
        hash = hash_byte(hash, prg_bank1);
        hash = hash_bytes(hash, video.chr_banks, 8);
        hash = hash_byte(hash, video.split_count);
        for (int i = 0; i < video.split_count; i++) {
            const VideoSplit *split = &video.splits[i];
            hash = hash_byte(hash, split->line);
            hash = hash_byte(hash, split->ctrl);
            hash = hash_byte(hash, split->mask);
            hash = hash_bytes(hash, split->chr_banks, 8);
            hash = hash_byte(hash, (uint8_t)split->x);
            hash = hash_byte(hash, (uint8_t)(split->x >> 8));
            hash = hash_byte(hash, (uint8_t)split->y);
            hash = hash_byte(hash, (uint8_t)(split->y >> 8));
        }
    }
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
