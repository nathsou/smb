#ifndef SMB_STATE_H
#define SMB_STATE_H

#include "external.h"
#include "cpu.h"
#include "ppu.h"
#include "apu.h"
#include <stdint.h>

#define SAVE_STATE_SIZE (RAM_SIZE + NAMETABLE_SIZE + PALETTE_SIZE + OAM_SIZE)
#define SAVE_STATE_NAMETABLE_OFFSET RAM_SIZE
#define SAVE_STATE_PALETTE_OFFSET (RAM_SIZE + NAMETABLE_SIZE)
#define SAVE_STATE_OAM_OFFSET (RAM_SIZE + NAMETABLE_SIZE + PALETTE_SIZE)

extern uint8_t last_save_state[SAVE_STATE_SIZE];
void save_state(uint8_t *dest);
void load_state(uint8_t *state);

#endif
