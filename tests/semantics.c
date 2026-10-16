// Checks the C the generator emits for 6502 instructions (tests/semantics.asm)
// against independent models, plus the runtime's stack, status and bus
// helpers. Built by tests/check_fixtures.py.
#include "lib/code.h"
#include "lib/ppu_bus.h"
#include <assert.h>
#include <stdio.h>

static int signed_byte(unsigned v) { return v < 128 ? (int)v : (int)v - 256; }

#define C 0x01
#define Z 0x02
#define V 0x40
#define N 0x80

static uint8_t status(void) { return ram[3]; }

static void run(void (*routine)(void), uint8_t lhs, uint8_t rhs, bool carry) {
    ram[0] = lhs;
    ram[1] = rhs;
    carry_flag = carry;
    routine();
}

static void check_nz(uint8_t value) {
    assert(((status() & Z) != 0) == (value == 0));
    assert(((status() & N) != 0) == (value >= 0x80));
}

int main(void) {
    cpu_init();
    sp = 0xff;
    // The NES 6502 has no decimal mode: arithmetic stays binary with D set.
    decimal_flag = true;

    for (unsigned lhs = 0; lhs < 256; lhs++) {
        for (unsigned rhs = 0; rhs < 256; rhs++) {
            for (unsigned carry = 0; carry < 2; carry++) {
                unsigned sum = lhs + rhs + carry;
                int signed_sum = signed_byte(lhs) + signed_byte(rhs) + (int)carry;
                run(Add, (uint8_t)lhs, (uint8_t)rhs, carry);
                assert(ram[2] == (uint8_t)sum && ((status() & C) != 0) == (sum > 255));
                assert(((status() & V) != 0) == (signed_sum < -128 || signed_sum > 127));
                check_nz(ram[2]);

                int difference = (int)lhs - (int)rhs - (1 - (int)carry);
                int signed_difference = signed_byte(lhs) - signed_byte(rhs) - (1 - (int)carry);
                run(Subtract, (uint8_t)lhs, (uint8_t)rhs, carry);
                assert(ram[2] == (uint8_t)difference && ((status() & C) != 0) == (difference >= 0));
                assert(((status() & V) != 0) == (signed_difference < -128 || signed_difference > 127));
                check_nz(ram[2]);
            }

            run(Compare, (uint8_t)lhs, (uint8_t)rhs, false);
            assert(((status() & C) != 0) == (lhs >= rhs));
            check_nz((uint8_t)(lhs - rhs));

            run(BitTest, (uint8_t)lhs, (uint8_t)rhs, false);
            assert(((status() & Z) != 0) == ((lhs & rhs) == 0));
            assert((status() & (N | V)) == (rhs & (N | V)));
        }

        for (unsigned carry = 0; carry < 2; carry++) {
            run(RotateLeft, (uint8_t)lhs, 0, carry);
            assert(ram[2] == (uint8_t)((lhs << 1) | carry) && ((status() & C) != 0) == (lhs >= 0x80));
            check_nz(ram[2]);

            run(RotateRight, (uint8_t)lhs, 0, carry);
            assert(ram[2] == (uint8_t)((lhs >> 1) | (carry << 7)) && ((status() & C) != 0) == (lhs & 1));
            check_nz(ram[2]);

            run(RotateMemory, (uint8_t)lhs, 0, carry);
            assert(ram[0] == (uint8_t)((lhs >> 1) | (carry << 7)) && ((status() & C) != 0) == (lhs & 1));
            check_nz(ram[0]);
        }

        run(ShiftMemory, (uint8_t)lhs, 0, false);
        assert(ram[0] == (uint8_t)(lhs << 1) && ((status() & C) != 0) == (lhs >= 0x80));
        check_nz(ram[0]);
    }

    ram[1] = 0xff; // $11 + $f0 wraps to $01
    IncrementIndexed();
    assert(ram[1] == 0 && ram[0x101] == 0 && (status() & Z));

    carry_flag = true;
    ShiftBus();
    assert((status() & (C | Z)) == Z);

    // PHP/PLP round trip, with the B and unused bits set in the pushed byte
    uint8_t saved_sp = sp;
    carry_flag = zero_flag = neg_flag = overflow_flag = true;
    php();
    assert(ram[0x100 + saved_sp] == (0x30 | C | Z | V | N | 0x04 | 0x08));
    carry_flag = zero_flag = neg_flag = overflow_flag = false;
    plp();
    assert(sp == saved_sp && carry_flag && zero_flag && neg_flag && overflow_flag);

    cpu_push_return_address(0x8123);
    assert(ram[0x100 + saved_sp] == 0x81 && ram[0x100 + (uint8_t)(saved_sp - 1)] == 0x23);
    assert(cpu_return_target() == 0x8124 && sp == saved_sp);

    dynamic_ram_write(0x1801, 0x55);
    assert(ram[1] == 0x55 && read_byte(0x801) == 0x55);
    dynamic_ram_write(0x3ffb, 0x21); // mirrored OAMADDR
    assert(oam_addr == 0x21);
    dynamic_ram_write(0x2004, 0x42);
    assert(video.oam[0x21] == 0x42 && oam_addr == 0x22);
    ram[0xff] = 0x34;
    ram[0] = 0x12;
    y = 2;
    assert(indirect_y_addr(0xff) == 0x1236);
    x = 1;
    assert(indirect_x_addr(0xfe) == 0x1234);
    ram[0xff] = 0x78;
    ram[0] = 0x56;
    ram[0x100] = 0x99;
    assert(read_indirect_word(0x00ff) == 0x5678);
    puts("Instruction semantics, status, stack and bus checks passed");
}
