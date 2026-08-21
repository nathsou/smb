/* Static 6502 lowering support.  code.c contains one compile-time-selected
 * case per assembled instruction; this file intentionally has no opcode
 * decoder or PRG fetch loop. */

static bool c6502_overflow;
static bool c6502_interrupt_disable;
static bool c6502_decimal;

static void c6502_set_nz(uint8_t value) {
    zero_flag = value == 0;
    neg_flag = (value & 0x80u) != 0;
}

static void c6502_push(uint8_t value) {
    dynamic_ram_write((uint16_t)(0x100u | sp), value);
    sp--;
}

static uint8_t c6502_pop(void) {
    sp++;
    return read_byte((uint16_t)(0x100u | sp));
}

static void c6502_push_word(uint16_t value) {
    c6502_push((uint8_t)(value >> 8));
    c6502_push((uint8_t)value);
}

static uint16_t c6502_pop_word(void) {
    uint16_t low = c6502_pop();
    return (uint16_t)(low | ((uint16_t)c6502_pop() << 8));
}

static uint8_t c6502_status(bool break_flag) {
    return (uint8_t)((carry_flag ? 1u : 0u) |
                     (zero_flag ? 2u : 0u) |
                     (c6502_interrupt_disable ? 4u : 0u) |
                     (c6502_decimal ? 8u : 0u) |
                     (break_flag ? 0x10u : 0u) | 0x20u |
                     (c6502_overflow ? 0x40u : 0u) |
                     (neg_flag ? 0x80u : 0u));
}

static void c6502_set_status(uint8_t value) {
    carry_flag = (value & 1u) != 0;
    zero_flag = (value & 2u) != 0;
    c6502_interrupt_disable = (value & 4u) != 0;
    c6502_decimal = (value & 8u) != 0;
    c6502_overflow = (value & 0x40u) != 0;
    neg_flag = (value & 0x80u) != 0;
}

static uint16_t c6502_read_word_zp(uint8_t address) {
    return (uint16_t)(read_byte(address) |
                      ((uint16_t)read_byte((uint8_t)(address + 1u)) << 8));
}

static uint16_t c6502_indirect_x(uint8_t address) {
    return c6502_read_word_zp((uint8_t)(address + x));
}

static uint16_t c6502_indirect_y(uint8_t address) {
    return (uint16_t)(c6502_read_word_zp(address) + y);
}

static void c6502_compare(uint8_t left, uint8_t right) {
    carry_flag = left >= right;
    c6502_set_nz((uint8_t)(left - right));
}

static void c6502_adc(uint8_t value) {
    uint16_t sum = (uint16_t)a + value + (carry_flag ? 1u : 0u);
    uint8_t result = (uint8_t)sum;
    c6502_overflow = ((~(a ^ value) & (a ^ result)) & 0x80u) != 0;
    carry_flag = sum > 0xffu;
    a = result;
    c6502_set_nz(a);
}

static uint8_t c6502_shift(uint8_t value, uint8_t kind) {
    bool old_carry = carry_flag;
    switch (kind) {
    case 0: carry_flag = (value & 0x80u) != 0; value = (uint8_t)(value << 1); break;
    case 1: carry_flag = (value & 0x80u) != 0; value = (uint8_t)(((uint32_t)value << 1) | (old_carry ? 1u : 0u)); break;
    case 2: carry_flag = (value & 1u) != 0; value >>= 1; break;
    default: carry_flag = (value & 1u) != 0; value = (uint8_t)((value >> 1) | (old_carry ? 0x80u : 0u)); break;
    }
    c6502_set_nz(value);
    return value;
}

static uint16_t c6502_indirect_jmp(uint16_t address) {
    uint16_t next = (uint16_t)((address & 0xff00u) | ((address + 1u) & 0xffu));
    return (uint16_t)(read_byte(address) | ((uint16_t)read_byte(next) << 8));
}
