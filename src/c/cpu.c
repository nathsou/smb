#include "cpu.h"
#include "data.h"
#include "code.h"
#include "ppu.h"
#include "apu.h"

uint8_t a;
uint8_t x;
uint8_t y;
uint8_t sp;

bool carry_flag;
bool zero_flag;
bool neg_flag;

size_t jsr_return_stack[JSR_STACK_SIZE];
size_t jsr_return_stack_top;
uint8_t ram[2048];

uint8_t controller1_state;
bool controller1_strobe;
uint8_t controller1_btn_index;

inline void update_controller1(uint8_t state) {
    controller1_state = state;
}

void cpu_init(void) {
    // registers
    a = 0;
    x = 0;
    y = 0;
    sp = 0xff;

    // flags
    carry_flag = false;
    zero_flag = false;
    neg_flag = false;

    // jsr return stack
    jsr_return_stack_top = 0;

    // controller
    controller1_state = 0;
    controller1_strobe = false;
    controller1_btn_index = 0;
}

uint8_t read_byte(uint16_t addr) {
    if (addr < 0x2000) {
        return ram[addr & 0b0000011111111111];
    }

    if (addr < 0x4000) {
        return ppu_read_register(0x2000 + (addr & 0b111));
    }

    if (addr == 0x4016) {
        if (controller1_btn_index > 7) {
            return 1;
        }

        uint8_t state = (controller1_state & (1 << controller1_btn_index)) >> controller1_btn_index;

        if (!controller1_strobe && controller1_btn_index < 8) {
            controller1_btn_index++;
        }
        
        return state;
    }

    if (addr < 0x4020) {
        // APU
    }

    if (addr >= 0x8000) {
        return data[addr - 0x8000];
    }

    return 0;
}

void write_byte(uint16_t addr, uint8_t value) {
    if (addr < 0x2000) {
        ram[addr & 0b0000011111111111] = value;
    } else if (addr < 0x4000) {
        ppu_write_register(0x2000 + (addr & 0b111), value);
    } else if (addr == 0x4014) {
        uint16_t start_addr = (uint16_t)(value << 8);
        ppu_transfer_oam(start_addr);
    } else if (addr == 0x4016) {
        // controller 1
        controller1_strobe = (value & 1) == 1;
        
        if (controller1_strobe) {
            controller1_btn_index = 0;
        }
    } else if (addr < 0x4020) {
        apu_write(addr, value);
    }
}

uint16_t read_word(uint16_t addr) {
    // little endian
    uint16_t low_byte = (uint16_t)read_byte(addr);
    uint16_t high_byte = (uint16_t)(((uint16_t)read_byte(addr + 1)) << 8);
    uint16_t word = high_byte | low_byte;
    return word;
}

void write_word(uint16_t addr, uint16_t value) {
    write_byte(addr, value & 0xff);
    write_byte(addr + 1, value >> 8);
}

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

uint16_t indirect_x_addr(uint8_t addr) {
    return read_word(addr + x);
}

uint16_t indirect_y_addr(uint8_t addr) {
    return read_word(addr) + y;
}

uint8_t indirect_x_val(uint8_t addr) {
    return read_byte(indirect_x_addr(addr));
}

uint8_t indirect_y_val(uint8_t addr) {
    return read_byte(indirect_y_addr(addr));
}

void next_frame(void) {
    smb(RUN_STATE_NMI_HANDLER);
}
