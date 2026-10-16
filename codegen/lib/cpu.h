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
extern bool overflow_flag, interrupt_disabled, decimal_flag;
extern uint16_t cpu_resume_pc;

// Inline-table dispatchers read their table through the JSR return address,
// so they push it on the guest stack themselves. Other calls are C calls.
void cpu_push_return_address(uint16_t return_address);
void cpu_yield(uint16_t pc);
void cpu_unresolved_jump(uint16_t pc);
void cpu_unsupported_io(uint16_t addr);

// PHP/PLP
void php(void);
void plp(void);
uint16_t cpu_return_target(void);
extern uint16_t cpu_dispatch_target;

// memory
extern uint8_t ram[RAM_SIZE];

// The game: PRG ROM (from the generated data.c, or loaded at run time into
// prg.c's buffer) and the generated entry points.
extern const uint8_t data[];
extern const unsigned prg_image_size;
void cpu_reset_entry(void);
void cpu_nmi_entry(void);

uint8_t read_byte(uint16_t addr);
void dynamic_ram_write(uint16_t addr, uint8_t value);

uint16_t read_word(uint16_t addr);
uint16_t read_indirect_word(uint16_t addr);
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
uint16_t absolute_x_addr(uint16_t addr);
uint8_t absolute_x(uint16_t addr);
uint8_t absolute_y(uint16_t addr);

uint16_t indirect_x_addr(uint8_t addr);
uint16_t indirect_y_addr(uint8_t addr);

uint8_t indirect_x_val(uint8_t addr);
uint8_t indirect_y_val(uint8_t addr);

// engine
void next_frame(void);

#endif
