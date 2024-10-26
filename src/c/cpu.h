#ifndef SMB_CPU_H
#define SMB_CPU_H

#include <stdint.h>

// registers
uint8_t a;
uint8_t x;
uint8_t y;
uint8_t sp;
uint16_t pc;

// flags
uint8_t carry_flag;
uint8_t zero_flag;
uint8_t neg_flag;
uint8_t overflow_flag;

// jsr return stack
int jsr_return_stack[255]; // TODO: experiment with the stack size
int jsr_return_stack_top;

void push_jsr(int return_index);
int pop_jsr();

#define jsr(target, return_index) push_jsr(return_index); goto target; jsr_ret_##return_index:

#endif
