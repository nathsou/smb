#include "../codegen/lib/instructions.h"
#include "../codegen/lib/ppu.h"
#include <assert.h>
#include <stdio.h>

static int signed_byte(unsigned v) { return v < 128 ? (int)v : (int)v - 256; }

int main(void) {
    cpu_init();
    // Independent arithmetic oracle, including the NES's binary arithmetic
    // when the decimal status bit is set. Exercise every input and carry-in.
    decimal_flag = true;
    for (unsigned lhs = 0; lhs < 256; ++lhs) {
        for (unsigned rhs = 0; rhs < 256; ++rhs) {
            for (unsigned carry = 0; carry < 2; ++carry) {
                unsigned sum = lhs + rhs + carry;
                int signed_sum = signed_byte(lhs) + signed_byte(rhs) + (int)carry;
                a = (uint8_t)lhs;
                carry_flag = carry != 0;
                adc_imm_fczn((uint8_t)rhs);
                assert(a == (uint8_t)sum && carry_flag == (sum > 255));
                assert(zero_flag == (a == 0) && neg_flag == (a >= 128));
                assert(overflow_flag == (signed_sum < -128 || signed_sum > 127));

                int difference = (int)lhs - (int)rhs - (1 - (int)carry);
                int signed_difference = signed_byte(lhs) - signed_byte(rhs) - (1 - (int)carry);
                a = (uint8_t)lhs;
                carry_flag = carry != 0;
                sbc_imm_fczn((uint8_t)rhs);
                assert(a == (uint8_t)difference && carry_flag == (difference >= 0));
                assert(zero_flag == (a == 0) && neg_flag == (a >= 128));
                assert(overflow_flag == (signed_difference < -128 || signed_difference > 127));
            }
        }
    }

    a = 0x0f;
    ram[0x20] = 0xc0;
    carry_flag = true;
    bit_zp_fzn(0x20);
    assert(zero_flag && neg_flag && overflow_flag && carry_flag);
    uint8_t saved_sp = sp;
    php();
    carry_flag = zero_flag = neg_flag = overflow_flag = false;
    plp();
    assert(sp == saved_sp && carry_flag && zero_flag && neg_flag && overflow_flag);

    cpu_call_begin(0x8123);
    assert(ram[0x100 + saved_sp] == 0x81);
    assert(ram[0x100 + (uint8_t)(saved_sp - 1)] == 0x23);
    cpu_call_end();
    assert(sp == saved_sp);

    dynamic_ram_write(0x1801, 0x55);
    assert(ram[1] == 0x55 && read_byte(0x801) == 0x55);
    dynamic_ram_write(0x3ffb, 0x21); // mirrored OAMADDR
    assert(oam_addr == 0x21);
    dynamic_ram_write(0x2004, 0x42);
    assert(oam[0x21] == 0x42 && oam_addr == 0x22);
    ram[0xff] = 0x34;
    ram[0] = 0x12;
    y = 2;
    assert(indirect_y_addr(0xff) == 0x1236);
    x = 1;
    assert(indirect_x_addr(0xfe) == 0x1234);
    puts("CPU arithmetic, status, stack, and bus checks passed");
}
