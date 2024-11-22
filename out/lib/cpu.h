#ifndef SMB_CPU_H
#define SMB_CPU_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#define RAM_SIZE 2048 // 2KB

// registers
extern uint8_t a, x, y, sp;

// flags
extern bool carry_flag, zero_flag, neg_flag; 
// the overflow flag is never used :)

// memory
extern uint8_t ram[RAM_SIZE];

uint8_t read_byte(uint16_t addr);
void dynamic_ram_write(uint16_t addr, uint8_t value);

uint16_t read_word(uint16_t addr);
void write_word(uint16_t addr, uint16_t value);

// controllers
extern uint8_t controller1_state, controller2_state;
void update_controller1(uint8_t state);
void write_joypad1(uint8_t value);
void write_joypad2(uint8_t value);

void cpu_init(void);

// addressing mode utils

void update_nz(uint8_t value);

uint8_t zero_page(uint8_t addr);
uint8_t zero_page_x(uint8_t addr);
uint8_t zero_page_y(uint8_t addr);

uint8_t absolute(uint16_t addr);
uint8_t absolute_x(uint16_t addr);
uint8_t absolute_y(uint16_t addr);

uint16_t indirect_x_addr(uint8_t addr);
uint16_t indirect_y_addr(uint8_t addr);

uint8_t indirect_x_val(uint8_t addr);
uint8_t indirect_y_val(uint8_t addr);

// engine
void next_frame(void);

#endif
