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

// STA - Store Accumulator

void sta_zp(uint8_t addr) {
    write_byte(addr, a);
}

void sta_zpx(uint8_t addr) {
    write_byte(addr + x, a);
}

void sta_zpy(uint8_t addr) {
    write_byte(addr + y, a);
}

void sta_abs(uint16_t addr) {
    write_byte(addr, a);
}

void sta_absx(uint16_t addr) {
    write_byte(addr + x, a);
}

void sta_absy(uint16_t addr) {
    write_byte(addr + y, a);
}

void sta_indy(uint8_t addr) {
    write_byte(indirect_y_addr(addr), a);
}

// STX - Store X Register

void stx_zp(uint8_t addr) {
    write_byte(zero_page(addr), x);
}

void stx_zpy(uint8_t addr) {
    write_byte(zero_page_y(addr), x);
}

void stx_abs(uint16_t addr) {
    write_byte(absolute(addr), x);
}

// STY - Store Y Register

void sty_zp(uint8_t addr) {
    write_byte(zero_page(addr), y);
}

void sty_zpx(uint8_t addr) {
    write_byte(zero_page_x(addr), y);
}

void sty_abs(uint16_t addr) {
    write_byte(absolute(addr), y);
}

// ADC - Add with Carry

void adc_imm(uint8_t value) {
    uint16_t sum = a + value + (carry_flag ? 1 : 0);
    carry_flag = sum > 0xff;
    a = sum;
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

void tax() {
    x = a;
    update_nz(x);
}

// TAY - Transfer Accumulator to Y

void tay() {
    y = a;
    update_nz(y);
}

// TSX - Transfer Stack Pointer to X

void tsx() {
    x = sp;
    update_nz(x);
}

// TXA - Transfer X to Accumulator

void txa() {
    a = x;
    update_nz(a);
}

// TXS - Transfer X to Stack Pointer

void txs() {
    sp = x;
}

// TYA - Transfer Y to Accumulator

void tya() {
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

void asl_acc() {
    carry_flag = a & 0x80;
    a <<= 1;
    update_nz(a);
}

void asl_abs(uint16_t addr) {
    uint8_t val = read_byte(addr);
    carry_flag = val & 0x80;
    val <<= 1;
    write_byte(addr, val);
    update_nz(val);
}

// LSR - Logical Shift Right

void lsr_acc() {
    carry_flag = a & 1;
    a >>= 1;
    update_nz(a);
}

void lsr_zp(uint8_t addr) {
    uint8_t val = zero_page(addr);
    carry_flag = val & 1;
    val >>= 1;
    write_byte(addr, val);
    update_nz(val);
}

void lsr_abs(uint16_t addr) {
    uint8_t val = absolute(addr);
    carry_flag = val & 1;
    val >>= 1;
    write_byte(addr, val);
    update_nz(val);
}

// INC - Increment Memory

void inc_zp(uint8_t addr) {
    uint8_t val = zero_page(addr);
    val++;
    write_byte(addr, val);
    update_nz(val);
}

void inc_zpx(uint8_t addr) {
    uint8_t val = zero_page_x(addr);
    val++;
    write_byte(addr, val);
    update_nz(val);
}

void inc_abs(uint16_t addr) {
    uint8_t val = absolute(addr);
    val++;
    write_byte(addr, val);
    update_nz(val);
}

void inc_absx(uint16_t addr) {
    uint8_t val = absolute_x(addr);
    val++;
    write_byte(addr, val);
    update_nz(val);
}

// INX - Increment X Register

void inx() {
    x++;
    update_nz(x);
}

// INY - Increment Y Register

void iny() {
    y++;
    update_nz(y);
}

// DEC - Decrement Memory

void dec_zp(uint8_t addr) {
    uint8_t val = zero_page(addr);
    val--;
    write_byte(addr, val);
    update_nz(val);
}

void dec_zpx(uint8_t addr) {
    uint8_t val = zero_page_x(addr);
    val--;
    write_byte(addr, val);
    update_nz(val);
}

void dec_abs(uint16_t addr) {
    uint8_t val = absolute(addr);
    val--;
    write_byte(addr, val);
    update_nz(val);
}

void dec_absx(uint16_t addr) {
    uint8_t val = absolute_x(addr);
    val--;
    write_byte(addr, val);
    update_nz(val);
}

// DEX - Decrement X Register

void dex() {
    x--;
    update_nz(x);
}

// DEY - Decrement Y Register

void dey() {
    y--;
    update_nz(y);
}

// CLC - Clear Carry Flag

void clc() {
    carry_flag = false;
}

// SEC - Set Carry Flag

void sec() {
    carry_flag = true;
}

// CLD - Clear Decimal Flag

void cld() {}

// SED - Set Decimal Flag

void sed() {}

// SEI - Set Interrupt Disable

void sei() {}

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

void pha() {
    write_byte(sp | 0x100, a);
    sp--;
}

// PLA - Pull Accumulator

void pla() {
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
    write_byte(addr, val);
    update_nz(val);
}

void rol_acc() {
    bool next_carry_flag = a & 0x80;
    a <<= 1;
    a |= carry_flag;
    carry_flag = next_carry_flag;
    update_nz(a);
}

void rol_zp(uint8_t addr) {
    _rol(zero_page(addr));
}

void rol_abs(uint16_t addr) {
    _rol(absolute(addr));
}

// ROR - Rotate Right

void ror_acc() {
    bool old_carry = carry_flag;
    carry_flag = a & 1;
    a >>= 1;

    if (old_carry) {
        a |= 0x80;
    }

    update_nz(a);
}

void ror_absx(uint16_t addr) {
    uint8_t val = absolute_x(addr);
    bool old_carry = carry_flag;
    carry_flag = val & 1;
    val >>= 1;

    if (old_carry) {
        val |= 0x80;
    }

    write_byte(addr, val);
    update_nz(val);
}
