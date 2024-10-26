#ifndef SMB_INSTRUCTIONS_H
#define SMB_INSTRUCTIONS_H

#include "cpu.h"

void lda_imm(uint8_t value);
void lda_zp(uint8_t addr);
void lda_zpx(uint8_t addr);
void lda_zpy(uint8_t addr);
void lda_abs(uint16_t addr);
void lda_absx(uint16_t addr);
void lda_absy(uint16_t addr);
void lda_indy(uint8_t addr);

void ldx_imm(uint8_t value);
void ldx_zp(uint8_t addr);
void ldx_zpy(uint8_t addr);
void ldx_abs(uint16_t addr);
void ldx_absy(uint16_t addr);

void ldy_imm(uint8_t value);
void ldy_zp(uint8_t addr);
void ldy_zpx(uint8_t addr);
void ldy_abs(uint16_t addr);
void ldy_absx(uint16_t addr);

void sta_zp(uint8_t addr);
void sta_zpx(uint8_t addr);
void sta_zpy(uint8_t addr);
void sta_abs(uint16_t addr);
void sta_absx(uint16_t addr);
void sta_absy(uint16_t addr);
void sta_indy(uint8_t addr);

void stx_zp(uint8_t addr);
void stx_zpy(uint8_t addr);
void stx_abs(uint16_t addr);

void sty_zp(uint8_t addr);
void sty_zpx(uint8_t addr);
void sty_abs(uint16_t addr);

void adc_imm(uint8_t value);
void adc_zp(uint8_t addr);
void adc_zpx(uint8_t addr);
void adc_zpy(uint8_t addr);
void adc_abs(uint16_t addr);
void adc_absx(uint16_t addr);
void adc_absy(uint16_t addr);

void sbc_imm(uint8_t value);
void sbc_zp(uint8_t addr);
void sbc_zpx(uint8_t addr);
void sbc_abs(uint16_t addr);
void sbc_absx(uint16_t addr);
void sbc_absy(uint16_t addr);

void tax();
void tay();
void tsx();
void txa();
void txs();
void tya();

void and_imm(uint8_t value);
void and_zp(uint8_t addr);
void and_abs(uint16_t addr);
void and_absx(uint16_t addr);
void and_absy(uint16_t addr);

void ora_imm(uint8_t value);
void ora_zp(uint8_t addr);
void ora_zpx(uint8_t addr);
void ora_zpy(uint8_t addr);
void ora_abs(uint16_t addr);
void ora_absx(uint16_t addr);
void ora_absy(uint16_t addr);

void eor_imm(uint8_t value);
void eor_zp(uint8_t addr);

void asl_acc();
void asl_abs(uint16_t addr);

void lsr_acc();
void lsr_zp(uint8_t addr);
void lsr_abs(uint16_t addr);

void inc_zp(uint8_t addr);
void inc_zpx(uint8_t addr);
void inc_abs(uint16_t addr);
void inc_absx(uint16_t addr);

void inx();
void iny();

void dec_zp(uint8_t addr);
void dec_zpx(uint8_t addr);
void dec_abs(uint16_t addr);
void dec_absx(uint16_t addr);

void dex();
void dey();

void clc();
void cld();

void sei();
void sec();
void sed();

void cmp_imm(uint8_t value);
void cmp_zp(uint8_t addr);
void cmp_zpx(uint8_t addr);
void cmp_zpy(uint8_t addr);
void cmp_abs(uint16_t addr);
void cmp_absx(uint16_t addr);
void cmp_absy(uint16_t addr);

void cpx_imm(uint8_t value);
void cpx_zp(uint8_t addr);

void cpy_imm(uint8_t value);
void cpy_zp(uint8_t addr);
void cpy_abs(uint16_t addr);

void pha();
void pla();

void bit_zp(uint8_t addr);
void bit_abs(uint16_t addr);

void rol_acc();
void rol_zp(uint8_t addr);
void rol_abs(uint16_t addr);

void ror_acc();
void ror_absx(uint16_t addr);

#endif
