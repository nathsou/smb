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
