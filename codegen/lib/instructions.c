#include "instructions.h"

// Generated code calls specialised flag-preserving or flag-free entry points.
// Both variants use the same inline semantic core, so each 6502 operation has
// one implementation while call sites remain predictable function calls. The
// flag mode is constant in every wrapper, so Clang folds the condition at -O3.

typedef enum {
    FLAGS_NONE = 0,
    FLAG_CARRY = 1 << 0,
    FLAG_ZERO = 1 << 1,
    FLAG_NEGATIVE = 1 << 2,
    FLAGS_NZ = FLAG_ZERO | FLAG_NEGATIVE,
    FLAGS_CNZ = FLAG_CARRY | FLAGS_NZ,
} FlagMask;

static inline void update_nz_masked(uint8_t value, FlagMask mask) {
    if (mask & FLAG_ZERO) {
        zero_flag = value == 0;
    }
    if (mask & FLAG_NEGATIVE) {
        neg_flag = (value & 0x80) != 0;
    }
}

#define DEFINE_READ_VARIANTS(name, arg_type, read_expr, operation, all_flags) \
    void name(arg_type arg) { operation((read_expr), all_flags); } \
    void name##_nf(arg_type arg) { operation((read_expr), FLAGS_NONE); } \
    void name##_fc(arg_type arg) { operation((read_expr), FLAG_CARRY); } \
    void name##_fz(arg_type arg) { operation((read_expr), FLAG_ZERO); } \
    void name##_fn(arg_type arg) { operation((read_expr), FLAG_NEGATIVE); } \
    void name##_fcz(arg_type arg) { operation((read_expr), FLAG_CARRY | FLAG_ZERO); } \
    void name##_fcn(arg_type arg) { operation((read_expr), FLAG_CARRY | FLAG_NEGATIVE); } \
    void name##_fzn(arg_type arg) { operation((read_expr), FLAGS_NZ); }

#define DEFINE_IMPLIED_VARIANTS(name, operation, all_flags) \
    void name(void) { operation(all_flags); } \
    void name##_nf(void) { operation(FLAGS_NONE); } \
    void name##_fc(void) { operation(FLAG_CARRY); } \
    void name##_fz(void) { operation(FLAG_ZERO); } \
    void name##_fn(void) { operation(FLAG_NEGATIVE); } \
    void name##_fcz(void) { operation(FLAG_CARRY | FLAG_ZERO); } \
    void name##_fcn(void) { operation(FLAG_CARRY | FLAG_NEGATIVE); } \
    void name##_fzn(void) { operation(FLAGS_NZ); }

// Loads

static inline void lda_value(uint8_t value, FlagMask mask) {
    a = value;
    update_nz_masked(a, mask);
}

DEFINE_READ_VARIANTS(lda_imm, uint8_t, arg, lda_value, FLAGS_NZ)
DEFINE_READ_VARIANTS(lda_zp, uint8_t, zero_page(arg), lda_value, FLAGS_NZ)
DEFINE_READ_VARIANTS(lda_zpx, uint8_t, zero_page_x(arg), lda_value, FLAGS_NZ)
DEFINE_READ_VARIANTS(lda_zpy, uint8_t, zero_page_y(arg), lda_value, FLAGS_NZ)
DEFINE_READ_VARIANTS(lda_abs, uint16_t, absolute(arg), lda_value, FLAGS_NZ)
DEFINE_READ_VARIANTS(lda_absx, uint16_t, absolute_x(arg), lda_value, FLAGS_NZ)
DEFINE_READ_VARIANTS(lda_absy, uint16_t, absolute_y(arg), lda_value, FLAGS_NZ)
DEFINE_READ_VARIANTS(lda_indy, uint8_t, indirect_y_val(arg), lda_value, FLAGS_NZ)

static inline void ldx_value(uint8_t value, FlagMask mask) {
    x = value;
    update_nz_masked(x, mask);
}

DEFINE_READ_VARIANTS(ldx_imm, uint8_t, arg, ldx_value, FLAGS_NZ)
DEFINE_READ_VARIANTS(ldx_zp, uint8_t, zero_page(arg), ldx_value, FLAGS_NZ)
DEFINE_READ_VARIANTS(ldx_zpy, uint8_t, zero_page_y(arg), ldx_value, FLAGS_NZ)
DEFINE_READ_VARIANTS(ldx_abs, uint16_t, absolute(arg), ldx_value, FLAGS_NZ)
DEFINE_READ_VARIANTS(ldx_absy, uint16_t, absolute_y(arg), ldx_value, FLAGS_NZ)

static inline void ldy_value(uint8_t value, FlagMask mask) {
    y = value;
    update_nz_masked(y, mask);
}

DEFINE_READ_VARIANTS(ldy_imm, uint8_t, arg, ldy_value, FLAGS_NZ)
DEFINE_READ_VARIANTS(ldy_zp, uint8_t, zero_page(arg), ldy_value, FLAGS_NZ)
DEFINE_READ_VARIANTS(ldy_zpx, uint8_t, zero_page_x(arg), ldy_value, FLAGS_NZ)
DEFINE_READ_VARIANTS(ldy_abs, uint16_t, absolute(arg), ldy_value, FLAGS_NZ)
DEFINE_READ_VARIANTS(ldy_absx, uint16_t, absolute_x(arg), ldy_value, FLAGS_NZ)

// Arithmetic and logic

static inline void adc_value(uint8_t value, FlagMask mask) {
    uint16_t sum = (uint16_t)a + (uint16_t)value + (uint16_t)carry_flag;
    a = (uint8_t)sum;
    if (mask & FLAG_CARRY) {
        carry_flag = (sum & 0x100) != 0;
    }
    update_nz_masked(a, mask);
}

DEFINE_READ_VARIANTS(adc_imm, uint8_t, arg, adc_value, FLAGS_CNZ)
DEFINE_READ_VARIANTS(adc_zp, uint8_t, zero_page(arg), adc_value, FLAGS_CNZ)
DEFINE_READ_VARIANTS(adc_zpx, uint8_t, zero_page_x(arg), adc_value, FLAGS_CNZ)
DEFINE_READ_VARIANTS(adc_zpy, uint8_t, zero_page_y(arg), adc_value, FLAGS_CNZ)
DEFINE_READ_VARIANTS(adc_abs, uint16_t, absolute(arg), adc_value, FLAGS_CNZ)
DEFINE_READ_VARIANTS(adc_absx, uint16_t, absolute_x(arg), adc_value, FLAGS_CNZ)
DEFINE_READ_VARIANTS(adc_absy, uint16_t, absolute_y(arg), adc_value, FLAGS_CNZ)

static inline void sbc_value(uint8_t value, FlagMask mask) {
    uint16_t diff = a - value - (carry_flag ? 0 : 1);
    a = (uint8_t)diff;
    if (mask & FLAG_CARRY) {
        carry_flag = diff <= 0xff;
    }
    update_nz_masked(a, mask);
}

DEFINE_READ_VARIANTS(sbc_imm, uint8_t, arg, sbc_value, FLAGS_CNZ)
DEFINE_READ_VARIANTS(sbc_zp, uint8_t, zero_page(arg), sbc_value, FLAGS_CNZ)
DEFINE_READ_VARIANTS(sbc_zpx, uint8_t, zero_page_x(arg), sbc_value, FLAGS_CNZ)
DEFINE_READ_VARIANTS(sbc_abs, uint16_t, absolute(arg), sbc_value, FLAGS_CNZ)
DEFINE_READ_VARIANTS(sbc_absx, uint16_t, absolute_x(arg), sbc_value, FLAGS_CNZ)
DEFINE_READ_VARIANTS(sbc_absy, uint16_t, absolute_y(arg), sbc_value, FLAGS_CNZ)

static inline void and_value(uint8_t value, FlagMask mask) {
    a &= value;
    update_nz_masked(a, mask);
}

DEFINE_READ_VARIANTS(and_imm, uint8_t, arg, and_value, FLAGS_NZ)
DEFINE_READ_VARIANTS(and_zp, uint8_t, zero_page(arg), and_value, FLAGS_NZ)
DEFINE_READ_VARIANTS(and_abs, uint16_t, absolute(arg), and_value, FLAGS_NZ)
DEFINE_READ_VARIANTS(and_absx, uint16_t, absolute_x(arg), and_value, FLAGS_NZ)
DEFINE_READ_VARIANTS(and_absy, uint16_t, absolute_y(arg), and_value, FLAGS_NZ)

static inline void ora_value(uint8_t value, FlagMask mask) {
    a |= value;
    update_nz_masked(a, mask);
}

DEFINE_READ_VARIANTS(ora_imm, uint8_t, arg, ora_value, FLAGS_NZ)
DEFINE_READ_VARIANTS(ora_zp, uint8_t, zero_page(arg), ora_value, FLAGS_NZ)
DEFINE_READ_VARIANTS(ora_zpx, uint8_t, zero_page_x(arg), ora_value, FLAGS_NZ)
DEFINE_READ_VARIANTS(ora_zpy, uint8_t, zero_page_y(arg), ora_value, FLAGS_NZ)
DEFINE_READ_VARIANTS(ora_abs, uint16_t, absolute(arg), ora_value, FLAGS_NZ)
DEFINE_READ_VARIANTS(ora_absx, uint16_t, absolute_x(arg), ora_value, FLAGS_NZ)
DEFINE_READ_VARIANTS(ora_absy, uint16_t, absolute_y(arg), ora_value, FLAGS_NZ)

static inline void eor_value(uint8_t value, FlagMask mask) {
    a ^= value;
    update_nz_masked(a, mask);
}

DEFINE_READ_VARIANTS(eor_imm, uint8_t, arg, eor_value, FLAGS_NZ)
DEFINE_READ_VARIANTS(eor_zp, uint8_t, zero_page(arg), eor_value, FLAGS_NZ)

// Register transfers and increments

static inline void tax_value(FlagMask mask) {
    x = a;
    update_nz_masked(x, mask);
}

static inline void tay_value(FlagMask mask) {
    y = a;
    update_nz_masked(y, mask);
}

static inline void tsx_value(FlagMask mask) {
    x = sp;
    update_nz_masked(x, mask);
}

static inline void txa_value(FlagMask mask) {
    a = x;
    update_nz_masked(a, mask);
}

static inline void tya_value(FlagMask mask) {
    a = y;
    update_nz_masked(a, mask);
}

DEFINE_IMPLIED_VARIANTS(tax, tax_value, FLAGS_NZ)
DEFINE_IMPLIED_VARIANTS(tay, tay_value, FLAGS_NZ)
DEFINE_IMPLIED_VARIANTS(tsx, tsx_value, FLAGS_NZ)
DEFINE_IMPLIED_VARIANTS(txa, txa_value, FLAGS_NZ)
DEFINE_IMPLIED_VARIANTS(tya, tya_value, FLAGS_NZ)

void txs(void) {
    sp = x;
}

static inline void inx_value(FlagMask mask) {
    x++;
    update_nz_masked(x, mask);
}

static inline void iny_value(FlagMask mask) {
    y++;
    update_nz_masked(y, mask);
}

static inline void dex_value(FlagMask mask) {
    x--;
    update_nz_masked(x, mask);
}

static inline void dey_value(FlagMask mask) {
    y--;
    update_nz_masked(y, mask);
}

DEFINE_IMPLIED_VARIANTS(inx, inx_value, FLAGS_NZ)
DEFINE_IMPLIED_VARIANTS(iny, iny_value, FLAGS_NZ)
DEFINE_IMPLIED_VARIANTS(dex, dex_value, FLAGS_NZ)
DEFINE_IMPLIED_VARIANTS(dey, dey_value, FLAGS_NZ)

// Shifts, rotates, and memory increments

static inline void asl_acc_value(FlagMask mask) {
    bool next_carry = (a & 0x80) != 0;
    a <<= 1;
    if (mask & FLAG_CARRY) {
        carry_flag = next_carry;
    }
    update_nz_masked(a, mask);
}

static inline void asl_memory(uint16_t addr, FlagMask mask) {
    uint8_t value = read_byte(addr);
    bool next_carry = (value & 0x80) != 0;
    value <<= 1;
    dynamic_ram_write(addr, value);
    if (mask & FLAG_CARRY) {
        carry_flag = next_carry;
    }
    update_nz_masked(value, mask);
}

DEFINE_IMPLIED_VARIANTS(asl_acc, asl_acc_value, FLAGS_CNZ)
DEFINE_READ_VARIANTS(asl_abs, uint16_t, arg, asl_memory, FLAGS_CNZ)

static inline void lsr_acc_value(FlagMask mask) {
    bool next_carry = (a & 1) != 0;
    a >>= 1;
    if (mask & FLAG_CARRY) {
        carry_flag = next_carry;
    }
    update_nz_masked(a, mask);
}

static inline void lsr_memory(uint16_t addr, FlagMask mask) {
    uint8_t value = read_byte(addr);
    bool next_carry = (value & 1) != 0;
    value >>= 1;
    dynamic_ram_write(addr, value);
    if (mask & FLAG_CARRY) {
        carry_flag = next_carry;
    }
    update_nz_masked(value, mask);
}

DEFINE_IMPLIED_VARIANTS(lsr_acc, lsr_acc_value, FLAGS_CNZ)
DEFINE_READ_VARIANTS(lsr_zp, uint8_t, (uint16_t)arg, lsr_memory, FLAGS_CNZ)
DEFINE_READ_VARIANTS(lsr_abs, uint16_t, arg, lsr_memory, FLAGS_CNZ)

static inline void inc_memory(uint16_t addr, FlagMask mask) {
    uint8_t value = read_byte(addr);
    value++;
    dynamic_ram_write(addr, value);
    update_nz_masked(value, mask);
}

DEFINE_READ_VARIANTS(inc_zp, uint8_t, (uint16_t)arg, inc_memory, FLAGS_NZ)
DEFINE_READ_VARIANTS(inc_zpx, uint8_t, (uint16_t)(arg + x), inc_memory, FLAGS_NZ)
DEFINE_READ_VARIANTS(inc_abs, uint16_t, arg, inc_memory, FLAGS_NZ)
DEFINE_READ_VARIANTS(inc_absx, uint16_t, arg + x, inc_memory, FLAGS_NZ)

static inline void dec_memory(uint16_t addr, FlagMask mask) {
    uint8_t value = read_byte(addr);
    value--;
    dynamic_ram_write(addr, value);
    update_nz_masked(value, mask);
}

DEFINE_READ_VARIANTS(dec_zp, uint8_t, (uint16_t)arg, dec_memory, FLAGS_NZ)
DEFINE_READ_VARIANTS(dec_zpx, uint8_t, (uint16_t)(arg + x), dec_memory, FLAGS_NZ)
DEFINE_READ_VARIANTS(dec_abs, uint16_t, arg, dec_memory, FLAGS_NZ)
DEFINE_READ_VARIANTS(dec_absx, uint16_t, arg + x, dec_memory, FLAGS_NZ)

static inline void rol_acc_value(FlagMask mask) {
    bool old_carry = carry_flag;
    bool next_carry = (a & 0x80) != 0;
    a = (uint8_t)(a << 1) | (uint8_t)old_carry;
    if (mask & FLAG_CARRY) {
        carry_flag = next_carry;
    }
    update_nz_masked(a, mask);
}

static inline void rol_memory(uint16_t addr, FlagMask mask) {
    uint8_t value = read_byte(addr);
    bool old_carry = carry_flag;
    bool next_carry = (value & 0x80) != 0;
    value = (uint8_t)(value << 1) | (uint8_t)old_carry;
    dynamic_ram_write(addr, value);
    if (mask & FLAG_CARRY) {
        carry_flag = next_carry;
    }
    update_nz_masked(value, mask);
}

DEFINE_IMPLIED_VARIANTS(rol_acc, rol_acc_value, FLAGS_CNZ)
DEFINE_READ_VARIANTS(rol_zp, uint8_t, (uint16_t)arg, rol_memory, FLAGS_CNZ)
DEFINE_READ_VARIANTS(rol_abs, uint16_t, arg, rol_memory, FLAGS_CNZ)

static inline void ror_acc_value(FlagMask mask) {
    bool old_carry = carry_flag;
    bool next_carry = (a & 1) != 0;
    a >>= 1;
    if (old_carry) {
        a |= 0x80;
    }
    if (mask & FLAG_CARRY) {
        carry_flag = next_carry;
    }
    update_nz_masked(a, mask);
}

static inline void ror_memory(uint16_t addr, FlagMask mask) {
    uint8_t value = read_byte(addr);
    bool old_carry = carry_flag;
    bool next_carry = (value & 1) != 0;
    value >>= 1;
    if (old_carry) {
        value |= 0x80;
    }
    dynamic_ram_write(addr, value);
    if (mask & FLAG_CARRY) {
        carry_flag = next_carry;
    }
    update_nz_masked(value, mask);
}

DEFINE_IMPLIED_VARIANTS(ror_acc, ror_acc_value, FLAGS_CNZ)
DEFINE_READ_VARIANTS(ror_absx, uint16_t, absolute_x_addr(arg), ror_memory, FLAGS_CNZ)

// Comparisons preserve memory reads even when their result flags are dead.

static inline void cmp_values(uint8_t lhs, uint8_t rhs, FlagMask mask) {
    if (mask & FLAG_CARRY) {
        carry_flag = lhs >= rhs;
    }
    update_nz_masked((uint8_t)(lhs - rhs), mask);
}

#define DEFINE_COMPARE_VARIANTS(name, arg_type, lhs, rhs) \
    void name(arg_type arg) { cmp_values((lhs), (rhs), FLAGS_CNZ); } \
    void name##_nf(arg_type arg) { cmp_values((lhs), (rhs), FLAGS_NONE); } \
    void name##_fc(arg_type arg) { cmp_values((lhs), (rhs), FLAG_CARRY); } \
    void name##_fz(arg_type arg) { cmp_values((lhs), (rhs), FLAG_ZERO); } \
    void name##_fn(arg_type arg) { cmp_values((lhs), (rhs), FLAG_NEGATIVE); } \
    void name##_fcz(arg_type arg) { cmp_values((lhs), (rhs), FLAG_CARRY | FLAG_ZERO); } \
    void name##_fcn(arg_type arg) { cmp_values((lhs), (rhs), FLAG_CARRY | FLAG_NEGATIVE); } \
    void name##_fzn(arg_type arg) { cmp_values((lhs), (rhs), FLAGS_NZ); }

DEFINE_COMPARE_VARIANTS(cmp_imm, uint8_t, a, arg)
DEFINE_COMPARE_VARIANTS(cmp_zp, uint8_t, a, zero_page(arg))
DEFINE_COMPARE_VARIANTS(cmp_zpx, uint8_t, a, zero_page_x(arg))
DEFINE_COMPARE_VARIANTS(cmp_zpy, uint8_t, a, zero_page_y(arg))
DEFINE_COMPARE_VARIANTS(cmp_abs, uint16_t, a, absolute(arg))
DEFINE_COMPARE_VARIANTS(cmp_absx, uint16_t, a, absolute_x(arg))
DEFINE_COMPARE_VARIANTS(cmp_absy, uint16_t, a, absolute_y(arg))
DEFINE_COMPARE_VARIANTS(cpx_imm, uint8_t, x, arg)
DEFINE_COMPARE_VARIANTS(cpx_zp, uint8_t, x, zero_page(arg))
DEFINE_COMPARE_VARIANTS(cpy_imm, uint8_t, y, arg)
DEFINE_COMPARE_VARIANTS(cpy_zp, uint8_t, y, zero_page(arg))
DEFINE_COMPARE_VARIANTS(cpy_abs, uint16_t, y, absolute(arg))

// Stack and BIT

void pha(void) {
    dynamic_ram_write(sp | 0x100, a);
    sp--;
}

static inline void pla_value(FlagMask mask) {
    sp++;
    a = read_byte(sp | 0x100);
    update_nz_masked(a, mask);
}

DEFINE_IMPLIED_VARIANTS(pla, pla_value, FLAGS_NZ)

static inline void bit_value(uint8_t value, FlagMask mask) {
    if (mask & FLAG_ZERO) {
        zero_flag = (a & value) == 0;
    }
    if (mask & FLAG_NEGATIVE) {
        neg_flag = (value & 0x80) != 0;
    }
}

DEFINE_READ_VARIANTS(bit_zp, uint8_t, zero_page(arg), bit_value, FLAGS_NZ)
DEFINE_READ_VARIANTS(bit_abs, uint16_t, absolute(arg), bit_value, FLAGS_NZ)

// Flag and processor-control instructions

void clc(void) {
    carry_flag = false;
}

void sec(void) {
    carry_flag = true;
}

void cld(void) {}
void sed(void) {}
void sei(void) {}

#undef DEFINE_COMPARE_VARIANTS
#undef DEFINE_IMPLIED_VARIANTS
#undef DEFINE_READ_VARIANTS
