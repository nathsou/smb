#include "instructions.h"

// LDA - Load Accumulator

void lda_imm(uint8_t value) {
    a = value;
    update_nz(a);
}

void lda_zp(uint8_t addr) {
    lda_imm(zero_page(addr));
}

void lda_zpx(uint8_t addr) {
    lda_imm(zero_page_x(addr));
}

void lda_zpy(uint8_t addr) {
    lda_imm(zero_page_y(addr));
}

void lda_abs(uint16_t addr) {
    lda_imm(absolute(addr));
}

void lda_absx(uint16_t addr) {
    lda_imm(absolute_x(addr));
}

void lda_absy(uint16_t addr) {
    lda_imm(absolute_y(addr));
}

void lda_indy(uint8_t addr) {
    lda_imm(indirect_y_val(addr));
}

// LDX - Load X Register

void ldx_imm(uint8_t value) {
    x = value;
    update_nz(x);
}

void ldx_zp(uint8_t addr) {
    ldx_imm(zero_page(addr));
}

void ldx_zpy(uint8_t addr) {
    ldx_imm(zero_page_y(addr));
}

void ldx_abs(uint16_t addr) {
    ldx_imm(absolute(addr));
}

void ldx_absy(uint16_t addr) {
    ldx_imm(absolute_y(addr));
}

// LDY - Load Y Register

void ldy_imm(uint8_t value) {
    y = value;
    update_nz(y);
}

void ldy_zp(uint8_t addr) {
    ldy_imm(zero_page(addr));
}

void ldy_zpx(uint8_t addr) {
    ldy_imm(zero_page_x(addr));
}

void ldy_abs(uint16_t addr) {
    ldy_imm(absolute(addr));
}

void ldy_absx(uint16_t addr) {
    ldy_imm(absolute_x(addr));
}

// ADC - Add with Carry

void adc_imm(uint8_t value) {
    uint16_t sum = (uint16_t)a + (uint16_t)value + (uint16_t)carry_flag;
    carry_flag = sum & 0x100;
    a = (uint8_t)sum;
    update_nz(a);
}

void adc_zp(uint8_t addr) {
    adc_imm(zero_page(addr));
}

void adc_zpx(uint8_t addr) {
    adc_imm(zero_page_x(addr));
}

void adc_zpy(uint8_t addr) {
    adc_imm(zero_page_y(addr));
}

void adc_abs(uint16_t addr) {
    adc_imm(absolute(addr));
}

void adc_absx(uint16_t addr) {
    adc_imm(absolute_x(addr));
}

void adc_absy(uint16_t addr) {
    adc_imm(absolute_y(addr));
}

// SBC - Subtract with Carry

void sbc_imm(uint8_t value) {
    uint16_t diff = a - value - (carry_flag ? 0 : 1);
    carry_flag = diff <= 0xff;
    a = diff & 0xff;
    update_nz(a);
}

void sbc_zp(uint8_t addr) {
    sbc_imm(zero_page(addr));
}

void sbc_zpx(uint8_t addr) {
    sbc_imm(zero_page_x(addr));
}

void sbc_abs(uint16_t addr) {
    sbc_imm(absolute(addr));
}

void sbc_absx(uint16_t addr) {
    sbc_imm(absolute_x(addr));
}

void sbc_absy(uint16_t addr) {
    sbc_imm(absolute_y(addr));
}

// TAX - Transfer Accumulator to X

void tax(void) {
    x = a;
    update_nz(x);
}

// TAY - Transfer Accumulator to Y

void tay(void) {
    y = a;
    update_nz(y);
}

// TSX - Transfer Stack Pointer to X

void tsx(void) {
    x = sp;
    update_nz(x);
}

// TXA - Transfer X to Accumulator

void txa(void) {
    a = x;
    update_nz(a);
}

// TXS - Transfer X to Stack Pointer

void txs(void) {
    sp = x;
}

// TYA - Transfer Y to Accumulator

void tya(void) {
    a = y;
    update_nz(a);
}

// AND - Logical AND

void and_imm(uint8_t value) {
    a &= value;
    update_nz(a);
}

void and_zp(uint8_t addr) {
    and_imm(zero_page(addr));
}

void and_abs(uint16_t addr) {
    and_imm(absolute(addr));
}

void and_absx(uint16_t addr) {
    and_imm(absolute_x(addr));
}

void and_absy(uint16_t addr) {
    and_imm(absolute_y(addr));
}

// ORA - Logical Inclusive OR

void ora_imm(uint8_t value) {
    a |= value;
    update_nz(a);
}

void ora_zp(uint8_t addr) {
    ora_imm(zero_page(addr));
}

void ora_zpx(uint8_t addr) {
    ora_imm(zero_page_x(addr));
}

void ora_zpy(uint8_t addr) {
    ora_imm(zero_page_y(addr));
}

void ora_abs(uint16_t addr) {
    ora_imm(absolute(addr));
}

void ora_absx(uint16_t addr) {
    ora_imm(absolute_x(addr));
}

void ora_absy(uint16_t addr) {
    ora_imm(absolute_y(addr));
}

// EOR - Exclusive OR

void eor_imm(uint8_t value) {
    a ^= value;
    update_nz(a);
}

void eor_zp(uint8_t addr) {
    eor_imm(zero_page(addr));
}

// ASL - Arithmetic Shift Left

void asl_acc(void) {
    carry_flag = a & 0x80;
    a <<= 1;
    update_nz(a);
}

void asl_abs(uint16_t addr) {
    uint8_t val = read_byte(addr);
    carry_flag = val & 0x80;
    val <<= 1;
    dynamic_ram_write(addr, val);
    update_nz(val);
}

// LSR - Logical Shift Right

void lsr_acc(void) {
    carry_flag = a & 1;
    a >>= 1;
    update_nz(a);
}

void _lsr(uint16_t addr) {
    uint8_t val = read_byte(addr);
    carry_flag = val & 1;
    val >>= 1;
    dynamic_ram_write(addr, val);
    update_nz(val);
}

void lsr_zp(uint8_t addr) {
    _lsr(addr);
}

void lsr_abs(uint16_t addr) {
    _lsr(addr);
}

// INC - Increment Memory

void _inc(uint16_t addr) {
    uint8_t val = read_byte(addr);
    val++;
    dynamic_ram_write(addr, val);
    update_nz(val);
}

void inc_zp(uint8_t addr) {
    _inc(addr);
}

void inc_zpx(uint8_t addr) {
    _inc(addr + x);
}

void inc_abs(uint16_t addr) {
    _inc(addr);
}

void inc_absx(uint16_t addr) {
    _inc(addr + x);
}

// INX - Increment X Register

void inx(void) {
    x++;
    update_nz(x);
}

// INY - Increment Y Register

void iny(void) {
    y++;
    update_nz(y);
}

// DEC - Decrement Memory

void _dec(uint16_t addr) {
    uint8_t val = read_byte(addr);
    val--;
    dynamic_ram_write(addr, val);
    update_nz(val);
}

void dec_zp(uint8_t addr) {
    _dec(addr);
}

void dec_zpx(uint8_t addr) {
    _dec(addr + x);
}

void dec_abs(uint16_t addr) {
    _dec(addr);
}

void dec_absx(uint16_t addr) {
    _dec(addr + x);
}

// DEX - Decrement X Register

void dex(void) {
    x--;
    update_nz(x);
}

// DEY - Decrement Y Register

void dey(void) {
    y--;
    update_nz(y);
}

// CLC - Clear Carry Flag

void clc(void) {
    carry_flag = false;
}

// SEC - Set Carry Flag

void sec(void) {
    carry_flag = true;
}

// CLD - Clear Decimal Flag

void cld(void) {}

// SED - Set Decimal Flag

void sed(void) {}

// SEI - Set Interrupt Disable

void sei(void) {}

void cmp_vals(uint8_t lhs, uint8_t rhs) {
    carry_flag = lhs >= rhs;
    update_nz(lhs - rhs);
}

// CMP - Compare

void cmp_imm(uint8_t value) {
    cmp_vals(a, value);
}

void cmp_zp(uint8_t addr) {
    cmp_vals(a, zero_page(addr));
}

void cmp_zpx(uint8_t addr) {
    cmp_vals(a, zero_page_x(addr));
}

void cmp_zpy(uint8_t addr) {
    cmp_vals(a, zero_page_y(addr));
}

void cmp_abs(uint16_t addr) {
    cmp_vals(a, absolute(addr));
}

void cmp_absx(uint16_t addr) {
    cmp_vals(a, absolute_x(addr));
}

void cmp_absy(uint16_t addr) {
    cmp_vals(a, absolute_y(addr));
}

// CPX - Compare X Register

void cpx_imm(uint8_t value) {
    cmp_vals(x, value);
}

void cpx_zp(uint8_t addr) {
    cmp_vals(x, zero_page(addr));
}

// CPY - Compare Y Register

void cpy_imm(uint8_t value) {
    cmp_vals(y, value);
}

void cpy_zp(uint8_t addr) {
    cmp_vals(y, zero_page(addr));
}

void cpy_abs(uint16_t addr) {
    cmp_vals(y, absolute(addr));
}

// PHA - Push Accumulator

void pha(void) {
    dynamic_ram_write(sp | 0x100, a);
    sp--;
}

// PLA - Pull Accumulator

void pla(void) {
    sp++;
    a = read_byte(sp | 0x100);
    update_nz(a);
}

// BIT - Bit Test

void _bit(uint8_t val) {
    zero_flag = (a & val) == 0;
    neg_flag = val & 0x80;
}

void bit_zp(uint8_t addr) {
    _bit(zero_page(addr));
}

void bit_abs(uint16_t addr) {
    _bit(absolute(addr));
}

// ROL - Rotate Left

void _rol(uint16_t addr) {
    uint8_t val = read_byte(addr);
    bool next_carry_flag = val & 0x80;
    val <<= 1;
    val |= carry_flag;
    carry_flag = next_carry_flag;
    dynamic_ram_write(addr, val);
    update_nz(val);
}

void rol_acc(void) {
    bool next_carry_flag = a & 0x80;
    a <<= 1;
    a |= carry_flag;
    carry_flag = next_carry_flag;
    update_nz(a);
}

void rol_zp(uint8_t addr) {
    _rol(addr);
}

void rol_abs(uint16_t addr) {
    _rol(addr);
}

// ROR - Rotate Right

void _ror(uint16_t addr) {
    uint8_t val = read_byte(addr);
    bool old_carry = carry_flag;
    carry_flag = val & 1;
    val >>= 1;

    if (old_carry) {
        val |= 0x80;
    }

    dynamic_ram_write(addr, val);
    update_nz(val);
}

inline void ror_acc(void) {
    bool old_carry = carry_flag;
    carry_flag = a & 1;
    a >>= 1;

    if (old_carry) {
        a |= 0x80;
    }

    update_nz(a);
}

inline void ror_absx(uint16_t addr) {
    _ror(absolute_x_addr(addr));
}

// Flag-free variants used by the static recompiler when all flags written by
// an instruction are dead. These still perform memory reads and writes, and
// still read the old carry where the 6502 instruction requires it.

void lda_imm_nf(uint8_t value) {
    a = value;
}

void lda_zp_nf(uint8_t addr) {
    lda_imm_nf(zero_page(addr));
}

void lda_zpx_nf(uint8_t addr) {
    lda_imm_nf(zero_page_x(addr));
}

void lda_zpy_nf(uint8_t addr) {
    lda_imm_nf(zero_page_y(addr));
}

void lda_abs_nf(uint16_t addr) {
    lda_imm_nf(absolute(addr));
}

void lda_absx_nf(uint16_t addr) {
    lda_imm_nf(absolute_x(addr));
}

void lda_absy_nf(uint16_t addr) {
    lda_imm_nf(absolute_y(addr));
}

void lda_indy_nf(uint8_t addr) {
    lda_imm_nf(indirect_y_val(addr));
}

void ldx_imm_nf(uint8_t value) {
    x = value;
}

void ldx_zp_nf(uint8_t addr) {
    ldx_imm_nf(zero_page(addr));
}

void ldx_zpy_nf(uint8_t addr) {
    ldx_imm_nf(zero_page_y(addr));
}

void ldx_abs_nf(uint16_t addr) {
    ldx_imm_nf(absolute(addr));
}

void ldx_absy_nf(uint16_t addr) {
    ldx_imm_nf(absolute_y(addr));
}

void ldy_imm_nf(uint8_t value) {
    y = value;
}

void ldy_zp_nf(uint8_t addr) {
    ldy_imm_nf(zero_page(addr));
}

void ldy_zpx_nf(uint8_t addr) {
    ldy_imm_nf(zero_page_x(addr));
}

void ldy_abs_nf(uint16_t addr) {
    ldy_imm_nf(absolute(addr));
}

void ldy_absx_nf(uint16_t addr) {
    ldy_imm_nf(absolute_x(addr));
}

void adc_imm_nf(uint8_t value) {
    uint16_t sum = (uint16_t)a + (uint16_t)value + (uint16_t)carry_flag;
    a = (uint8_t)sum;
}

void adc_zp_nf(uint8_t addr) {
    adc_imm_nf(zero_page(addr));
}

void adc_zpx_nf(uint8_t addr) {
    adc_imm_nf(zero_page_x(addr));
}

void adc_zpy_nf(uint8_t addr) {
    adc_imm_nf(zero_page_y(addr));
}

void adc_abs_nf(uint16_t addr) {
    adc_imm_nf(absolute(addr));
}

void adc_absx_nf(uint16_t addr) {
    adc_imm_nf(absolute_x(addr));
}

void adc_absy_nf(uint16_t addr) {
    adc_imm_nf(absolute_y(addr));
}

void sbc_imm_nf(uint8_t value) {
    uint16_t diff = a - value - (carry_flag ? 0 : 1);
    a = diff & 0xff;
}

void sbc_zp_nf(uint8_t addr) {
    sbc_imm_nf(zero_page(addr));
}

void sbc_zpx_nf(uint8_t addr) {
    sbc_imm_nf(zero_page_x(addr));
}

void sbc_abs_nf(uint16_t addr) {
    sbc_imm_nf(absolute(addr));
}

void sbc_absx_nf(uint16_t addr) {
    sbc_imm_nf(absolute_x(addr));
}

void sbc_absy_nf(uint16_t addr) {
    sbc_imm_nf(absolute_y(addr));
}

void tax_nf(void) {
    x = a;
}

void tay_nf(void) {
    y = a;
}

void tsx_nf(void) {
    x = sp;
}

void txa_nf(void) {
    a = x;
}

void tya_nf(void) {
    a = y;
}

void and_imm_nf(uint8_t value) {
    a &= value;
}

void and_zp_nf(uint8_t addr) {
    and_imm_nf(zero_page(addr));
}

void and_abs_nf(uint16_t addr) {
    and_imm_nf(absolute(addr));
}

void and_absx_nf(uint16_t addr) {
    and_imm_nf(absolute_x(addr));
}

void and_absy_nf(uint16_t addr) {
    and_imm_nf(absolute_y(addr));
}

void ora_imm_nf(uint8_t value) {
    a |= value;
}

void ora_zp_nf(uint8_t addr) {
    ora_imm_nf(zero_page(addr));
}

void ora_zpx_nf(uint8_t addr) {
    ora_imm_nf(zero_page_x(addr));
}

void ora_zpy_nf(uint8_t addr) {
    ora_imm_nf(zero_page_y(addr));
}

void ora_abs_nf(uint16_t addr) {
    ora_imm_nf(absolute(addr));
}

void ora_absx_nf(uint16_t addr) {
    ora_imm_nf(absolute_x(addr));
}

void ora_absy_nf(uint16_t addr) {
    ora_imm_nf(absolute_y(addr));
}

void eor_imm_nf(uint8_t value) {
    a ^= value;
}

void eor_zp_nf(uint8_t addr) {
    eor_imm_nf(zero_page(addr));
}

void asl_acc_nf(void) {
    a <<= 1;
}

void asl_abs_nf(uint16_t addr) {
    uint8_t val = read_byte(addr);
    val <<= 1;
    dynamic_ram_write(addr, val);
}

void lsr_acc_nf(void) {
    a >>= 1;
}

void lsr_zp_nf(uint8_t addr) {
    uint8_t val = read_byte(addr);
    val >>= 1;
    dynamic_ram_write(addr, val);
}

void lsr_abs_nf(uint16_t addr) {
    uint8_t val = read_byte(addr);
    val >>= 1;
    dynamic_ram_write(addr, val);
}

void inc_zp_nf(uint8_t addr) {
    uint8_t val = read_byte(addr);
    val++;
    dynamic_ram_write(addr, val);
}

void inc_zpx_nf(uint8_t addr) {
    inc_zp_nf(addr + x);
}

void inc_abs_nf(uint16_t addr) {
    uint8_t val = read_byte(addr);
    val++;
    dynamic_ram_write(addr, val);
}

void inc_absx_nf(uint16_t addr) {
    inc_abs_nf(addr + x);
}

void inx_nf(void) {
    x++;
}

void iny_nf(void) {
    y++;
}

void dec_zp_nf(uint8_t addr) {
    uint8_t val = read_byte(addr);
    val--;
    dynamic_ram_write(addr, val);
}

void dec_zpx_nf(uint8_t addr) {
    dec_zp_nf(addr + x);
}

void dec_abs_nf(uint16_t addr) {
    uint8_t val = read_byte(addr);
    val--;
    dynamic_ram_write(addr, val);
}

void dec_absx_nf(uint16_t addr) {
    dec_abs_nf(addr + x);
}

void dex_nf(void) {
    x--;
}

void dey_nf(void) {
    y--;
}

void cmp_imm_nf(uint8_t value) {
    (void)value;
}

void cmp_zp_nf(uint8_t addr) {
    (void)zero_page(addr);
}

void cmp_zpx_nf(uint8_t addr) {
    (void)zero_page_x(addr);
}

void cmp_zpy_nf(uint8_t addr) {
    (void)zero_page_y(addr);
}

void cmp_abs_nf(uint16_t addr) {
    (void)absolute(addr);
}

void cmp_absx_nf(uint16_t addr) {
    (void)absolute_x(addr);
}

void cmp_absy_nf(uint16_t addr) {
    (void)absolute_y(addr);
}

void cpx_imm_nf(uint8_t value) {
    (void)value;
}

void cpx_zp_nf(uint8_t addr) {
    (void)zero_page(addr);
}

void cpy_imm_nf(uint8_t value) {
    (void)value;
}

void cpy_zp_nf(uint8_t addr) {
    (void)zero_page(addr);
}

void cpy_abs_nf(uint16_t addr) {
    (void)absolute(addr);
}

void pla_nf(void) {
    sp++;
    a = read_byte(sp | 0x100);
}

void bit_zp_nf(uint8_t addr) {
    (void)zero_page(addr);
}

void bit_abs_nf(uint16_t addr) {
    (void)absolute(addr);
}

void rol_acc_nf(void) {
    a = (uint8_t)(a << 1) | (uint8_t)carry_flag;
}

void rol_zp_nf(uint8_t addr) {
    uint8_t val = read_byte(addr);
    val = (uint8_t)(val << 1) | (uint8_t)carry_flag;
    dynamic_ram_write(addr, val);
}

void rol_abs_nf(uint16_t addr) {
    uint8_t val = read_byte(addr);
    val = (uint8_t)(val << 1) | (uint8_t)carry_flag;
    dynamic_ram_write(addr, val);
}

void ror_acc_nf(void) {
    bool old_carry = carry_flag;
    a >>= 1;
    if (old_carry) {
        a |= 0x80;
    }
}

void ror_absx_nf(uint16_t addr) {
    uint16_t target = absolute_x_addr(addr);
    uint8_t val = read_byte(target);
    bool old_carry = carry_flag;
    val >>= 1;
    if (old_carry) {
        val |= 0x80;
    }
    dynamic_ram_write(target, val);
}
