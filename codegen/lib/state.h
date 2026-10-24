#ifndef SMB_STATE_H
#define SMB_STATE_H

#include "external.h"
#include "cpu.h"
#include "video.h"
#include "sound.h"
#include <stdint.h>

#define SAVE_STATE_DISPLAY_SIZE (RAM_SIZE + NAMETABLE_SIZE + PALETTE_SIZE + OAM_SIZE)
#define SAVE_STATE_NAMETABLE_OFFSET RAM_SIZE
#define SAVE_STATE_PALETTE_OFFSET (RAM_SIZE + NAMETABLE_SIZE)
#define SAVE_STATE_OAM_OFFSET (RAM_SIZE + NAMETABLE_SIZE + PALETTE_SIZE)

#ifdef MAPPER
// Cartridges with a mapper (Metroid) also keep their tiles (CHR RAM), their
// RAM and the mapper, the PPU's registers and latches, and the CPU: the
// state is saved between frames, while the foreground is suspended in a
// wait, with its registers and its resume points.
#define SAVE_STATE_CARTRIDGE_SIZE (CHR_ROM_SIZE + 0x2000 + 6 + 6 + 6 + 12 + 1 + CPU_RESUME_STACK)
#define SAVE_STATE_SIZE (SAVE_STATE_DISPLAY_SIZE + SAVE_STATE_CARTRIDGE_SIZE)
#else
#define SAVE_STATE_SIZE SAVE_STATE_DISPLAY_SIZE
#endif

extern uint8_t last_save_state[SAVE_STATE_SIZE];
extern const uint32_t save_state_size; // for hosts that read memory (web)
void save_state(uint8_t *dest);
void load_state(uint8_t *state);

#endif
