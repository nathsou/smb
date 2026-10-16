#include "state.h"

uint8_t last_save_state[SAVE_STATE_SIZE];

void save_state(uint8_t *dest) {
    // TODO: investigate why memcpy has strange behavior in the web target
    for (size_t i = 0; i < RAM_SIZE; i++) {
        dest[i] = ram[i];
    }

    for (size_t i = 0; i < NAMETABLE_SIZE; i++) {
        dest[SAVE_STATE_NAMETABLE_OFFSET + i] = video.nametable[i];
    }

    for (size_t i = 0; i < PALETTE_SIZE; i++) {
        dest[SAVE_STATE_PALETTE_OFFSET + i] = video.palette.u8[i];
    }

    for (size_t i = 0; i < OAM_SIZE; i++) {
        dest[SAVE_STATE_OAM_OFFSET + i] = video.oam[i];
    }
}

void load_state(uint8_t *state) {
    memcpy(ram, state, RAM_SIZE);
    memcpy(video.nametable, state + SAVE_STATE_NAMETABLE_OFFSET, NAMETABLE_SIZE);
    memcpy(video.palette.u8, state + SAVE_STATE_PALETTE_OFFSET, PALETTE_SIZE);
    memcpy(video.oam, state + SAVE_STATE_OAM_OFFSET, OAM_SIZE);
}
