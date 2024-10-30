#ifndef SMB_APU_H
#define SMB_APU_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

void apu_init(size_t frequency);
void apu_write(uint16_t addr, uint8_t value);
void apu_step_frame(void);
void apu_fill_buffer(uint8_t* cb_buffer, size_t size);

#endif
