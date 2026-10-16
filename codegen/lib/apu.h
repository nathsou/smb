#ifndef SMB_APU_H
#define SMB_APU_H
#define AUDIO_BUFFER_SIZE (4 * 1024)

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

extern uint8_t audio_buffer[AUDIO_BUFFER_SIZE];
// reserve space for the web audio buffer
extern uint8_t web_audio_buffer[AUDIO_BUFFER_SIZE];
extern uint16_t audio_buffer_size;
extern uint16_t audio_buffer_index;

// FNV-1a over every register write (address low, high, value) since the
// last reset: the replay's oracle for what the game asks of the APU.
extern uint32_t apu_write_hash;
#define APU_WRITE_HASH_RESET 2166136261u

void apu_init(size_t frequency);
void apu_write(uint16_t addr, uint8_t value);
void apu_step_frame(void);
void apu_fill_buffer(uint8_t* cb_buffer, size_t size);

#endif
