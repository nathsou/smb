#ifndef SMB_SOUND_H
#define SMB_SOUND_H

// The game's side of the sound hardware: it writes the APU registers
// ($4000-$4017), which are collected per frame for the synthesizer (apu.h).
// The game never reads them back.

#include <stdint.h>
#include "apu.h"

extern uint8_t sound_events[APU_FRAME_EVENTS_MAX * 2]; // (register - $4000, value)
extern uint16_t sound_event_count;
// FNV-1a over every write (address low, high, value) since the last reset:
// the replay's oracle for what the game asks of the APU.
extern uint32_t apu_write_hash;
#define APU_WRITE_HASH_RESET 2166136261u

void apu_write(uint16_t addr, uint8_t value);
// Hands this frame's writes to the synthesizer and starts the next frame.
void sound_end_frame(void);

#endif
