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

// Exact returns (programs that read their return addresses): JSR pushes the
// return address and RTS pulls it; `cpu_return` is where the last return
// went, which the caller compares with where it called from.
extern uint16_t cpu_return;
void cpu_returned(uint8_t low, uint8_t high);

// Suspension in wait loops: the functions on the way to a wait push their
// resume points and return, down to the reset routine, which next_frame
// calls again after the interrupt (see the IR's escape.mbt).
#define CPU_RESUME_STACK 64
extern uint8_t cpu_suspended;
extern uint8_t cpu_resume_depth;
extern uint8_t cpu_resume_point;
extern uint8_t cpu_resume_stack[CPU_RESUME_STACK];
void cpu_suspend(uint8_t point);
void cpu_resume_pop(void);

// Restarts (a game replacing the stack pointer outside its reset routine,
// to start over from deep in its calls): the calls on the way unwind, down
// to the reset routine, which runs restart routine `cpu_restarting` (see the
// IR's restart.mbt).
extern uint8_t cpu_restarting;
void cpu_restart(uint8_t routine);
void cpu_restart_done(void);

// Preemption (a map's `preempt`): the foreground counts its transfers back
// (`cpu_poll`) and yields past the budget (`cpu_preempted`, see the IR's
// preempt.mbt); the count starts again each frame.
extern uint8_t cpu_preempted;
extern uint16_t cpu_polls, cpu_poll_budget;
void cpu_preemption(uint8_t low, uint8_t high);
void cpu_poll(void);

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
// The IRQ handler (cartridges whose mapper raises it: MMC3).
void cpu_irq_entry(void);
// CLI: interrupts enabled, which the frame model notes (an NMI handler that
// enables them is interrupted by the IRQ on the hardware).
extern uint8_t cpu_interrupts_were_enabled;
void cpu_interrupts_enabled(void);

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
