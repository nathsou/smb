#include "cpu.h"
#include "data.h"
#include <stdio.h>

#define CHR_ROM_OFFSET 0x8010 // iNES header (16 bytes) + 2 pages of PRG ROM (32KB)

uint8_t a;
uint8_t x;
uint8_t y;
uint8_t sp;

bool carry_flag;
bool zero_flag;
bool neg_flag;

size_t jsr_return_stack[255];
size_t jsr_return_stack_top;
// uint8_t chr_rom[8192];
uint8_t ram[2048];

void init_cpu(uint8_t* rom) {
    // registers
    a = 0;
    x = 0;
    y = 0;
    sp = 0xff;

    // flags
    carry_flag = 0;
    zero_flag = 0;
    neg_flag = 0;

    // jsr return stack
    jsr_return_stack_top = 0;
    // chr_rom = rom + CHR_ROM_OFFSET;
}

uint8_t read_byte(uint16_t addr) {
    if (addr < 0x2000) {
        return ram[addr & 0x7ff];
    }

    if (addr < 0x4018) {
        // PPU registers
        printf("TODO: read_byte: PPU register: %x\n", addr);
        return 0;
    }

    if (addr < 0x4020) {
        // APU
        printf("TODO: read_byte: APU register: %x\n", addr);
    }

    if (addr >= 0x8000) {
        return data[addr - 0x8000];
    }

    printf("read_byte: unhandled address: %x\n", addr);
    return 0;
}

void write_byte(uint16_t addr, uint8_t value) {
    if (addr < 0x2000) {
        ram[addr & 0x7ff] = value;
    } else if (addr < 0x4018) {
        // PPU registers
        printf("TODO: write_byte: PPU register: %x\n", addr);
    } else if (addr < 0x4020) {
        // APU
        printf("TODO: write_byte: APU register: %x\n", addr);
    } else {
        printf("write_byte: unhandled address: %x\n", addr);
    }
}

uint16_t read_word(uint16_t addr) { return 0; }

void write_word(uint16_t addr, uint16_t value) {}

void push_jsr(size_t return_index) {
    jsr_return_stack[jsr_return_stack_top++] = return_index;
}

size_t pop_jsr() {
    return jsr_return_stack[--jsr_return_stack_top];
}

// addressing mode utils

void update_nz(uint8_t value) {
    zero_flag = value == 0;
    neg_flag = (value & 0b010000000) != 0;
}

uint8_t zero_page(uint8_t addr) {
    return read_byte(addr);
}

uint8_t zero_page_x(uint8_t addr) {
    return read_byte((addr + x) & 0xff); // TODO: check if wrapping is necessary
}

uint8_t zero_page_y(uint8_t addr) {
    return read_byte(addr + y);
}

uint8_t absolute(uint16_t addr) {
    return read_byte(addr);
}

uint8_t absolute_x(uint16_t addr) {
    return read_byte(addr + x);
}

uint8_t absolute_y(uint16_t addr) {
    return read_byte(addr + y);
}

uint8_t indirect_x_addr(uint8_t addr) {
    return read_word(addr + x);
}

uint8_t indirect_y_addr(uint8_t addr) {
    return read_word(addr) + y;
}

uint8_t indirect_x_val(uint8_t addr) {
    return read_byte(indirect_x_addr(addr));
}

uint8_t indirect_y_val(uint8_t addr) {
    return read_byte(indirect_y_addr(addr));
}