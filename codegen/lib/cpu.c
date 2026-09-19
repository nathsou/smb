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
bool overflow_flag;
bool interrupt_disabled;
bool decimal_flag;
uint16_t cpu_resume_pc;

// C calls express normal continuations; the guest stack remains observable to
// PHA/PLA, TSX, memory accesses, and return-address-based inline dispatchers.
void cpu_call_begin(uint16_t return_address) {
    ram[0x100 + sp--] = (uint8_t)(return_address >> 8);
    ram[0x100 + sp--] = (uint8_t)return_address;
}

void cpu_call_end(uint16_t expected_return_address) {
    uint16_t actual = (uint16_t)(ram[0x100 + (uint8_t)(sp + 1)] |
        (ram[0x100 + (uint8_t)(sp + 2)] << 8));
    if (actual != expected_return_address) {
        cpu_unresolved_jump((uint16_t)(actual + 1));
    }
    sp = (uint8_t)(sp + 2);
}

void cpu_yield(uint16_t pc) {
    cpu_resume_pc = pc;
}

void cpu_unresolved_jump(uint16_t pc) {
    cpu_resume_pc = pc;
    __builtin_trap(); // explicit unsupported transfer, never silent fallthrough
}

uint8_t ram[2048];

uint8_t controller1_state;
bool controller1_strobe;
uint8_t controller1_btn_index;

uint8_t controller2_state;
bool controller2_strobe;
uint8_t controller2_btn_index;

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
    overflow_flag = false;
    interrupt_disabled = true;
    decimal_flag = false;
    cpu_resume_pc = 0;

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
        return data[(addr - 0x8000) % PRG_IMAGE_SIZE];
    }

    return 0;
}

void write_joypad1(uint8_t value) {
    controller1_strobe = (value & 1) == 1;
    
    if (controller1_strobe) {
        controller1_btn_index = 0;
    }
}

void write_joypad2(uint8_t value) {
    controller2_strobe = (value & 1) == 1;
    
    if (controller2_strobe) {
        controller2_btn_index = 0;
    }
}

void dynamic_ram_write(uint16_t addr, uint8_t value) {
    if (addr < 0x2000) {
        ram[addr & 0b0000011111111111] = value;
    } else if (addr < 0x4000) {
        ppu_write_register(addr, value);
    } else if (addr == 0x4014) {
        ppu_transfer_oam((uint16_t)(value << 8));
    } else if (addr == 0x4016) {
        write_joypad1(value);
        write_joypad2(value);
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
    dynamic_ram_write(addr, value & 0xff);
    dynamic_ram_write(addr + 1, value >> 8);
}

// addressing mode utils

void update_nz(uint8_t value) {
    zero_flag = value == 0;
    neg_flag = (value & 0b010000000) != 0;
}

inline uint8_t zero_page(uint8_t addr) {
    return read_byte((uint16_t)addr);
}

inline uint8_t zero_page_x_addr(uint8_t addr) {
    return addr + x;
}

inline uint8_t zero_page_x(uint8_t addr) {
    return read_byte((uint16_t)zero_page_x_addr(addr));
}

inline uint8_t zero_page_y_addr(uint8_t addr) {
    return addr + y;
}

inline uint8_t zero_page_y(uint8_t addr) {
    return read_byte((uint16_t)zero_page_y_addr(addr));
}

inline uint8_t absolute(uint16_t addr) {
    return read_byte(addr);
}

inline uint16_t absolute_x_addr(uint16_t addr) {
    return addr + x;
}

inline uint8_t absolute_x(uint16_t addr) {
    return read_byte(absolute_x_addr(addr));
}

inline uint8_t absolute_y(uint16_t addr) {
    return read_byte(addr + y);
}

inline uint16_t indirect_x_addr(uint8_t addr) {
    uint8_t addr1 = addr + x;
    uint8_t addr2 = addr1 + 1; // zero page wrap around
    uint16_t low_byte = (uint16_t)read_byte(addr1);
    uint16_t high_byte = (uint16_t)(((uint16_t)read_byte(addr2)) << 8);
    return high_byte | low_byte;
}

inline uint16_t indirect_y_addr(uint8_t addr) {
    uint8_t addr2 = addr + 1; // zero page wrap around
    uint16_t low_byte = (uint16_t)read_byte(addr);
    uint16_t high_byte = (uint16_t)(((uint16_t)read_byte(addr2)) << 8);
    return (high_byte | low_byte) + (uint16_t)y;
}

inline uint8_t indirect_x_val(uint8_t addr) {
    return read_byte(indirect_x_addr(addr));
}

inline uint8_t indirect_y_val(uint8_t addr) {
    return read_byte(indirect_y_addr(addr));
}

inline void next_frame(void) {
    // The frame adapter delivers NMI while the foreground is at a proven idle
    // yield. Hardware saves PC/P, not A/X/Y. Keep those stack bytes observable.
    uint16_t return_pc = cpu_resume_pc;
    cpu_call_begin(return_pc);
    php();
    ram[0x100 + (uint8_t)(sp + 1)] &= (uint8_t)~0x10; // hardware clears B
    interrupt_disabled = true;
    cpu_nmi_entry();
    plp();
    cpu_call_end(return_pc);
}
