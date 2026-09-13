#ifndef SMB_APU_H
#define SMB_APU_H
#define AUDIO_BUFFER_SIZE (4 * 1024)

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdatomic.h>

extern uint8_t audio_buffer[AUDIO_BUFFER_SIZE];
// reserve space for the web audio buffer
extern uint8_t web_audio_buffer[AUDIO_BUFFER_SIZE];
extern uint16_t audio_buffer_size;

void apu_init(size_t frequency);
void apu_write(uint16_t addr, uint8_t value);
void apu_step_frame(void);
void apu_fill_buffer(uint8_t* cb_buffer, size_t size);
size_t apu_buffered_samples(void);
size_t apu_take_underrun_samples(void);
void apu_clear_buffer(void);

#endif
