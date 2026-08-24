#ifndef SMB_INSTRUCTIONS_H
#define SMB_INSTRUCTIONS_H

#include "cpu.h"

// Instruction helpers are an implementation detail of generated code. Keeping
// them hidden prevents --export-all WebAssembly builds from retaining every
// specialised flag variant.
#if defined(__GNUC__)
#pragma GCC visibility push(hidden)
#endif

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

void tax(void);
void tay(void);
void tsx(void);
void txa(void);
void txs(void);
void tya(void);

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

void inx(void);
void iny(void);

void dec_zp(uint8_t addr);
void dec_zpx(uint8_t addr);
void dec_abs(uint16_t addr);
void dec_absx(uint16_t addr);

void dex(void);
void dey(void);

void clc(void);
void cld(void);

void sei(void);
void sec(void);
void sed(void);

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

void pha(void);
void pla(void);

void bit_zp(uint8_t addr);
void bit_abs(uint16_t addr);

void rol_acc(void);
void rol_zp(uint8_t addr);
void rol_abs(uint16_t addr);

void ror_acc(void);
void ror_absx(uint16_t addr);

// Flag-setting variants. The suffix lists exactly the flags written:
// c = carry, z = zero, n = negative. An unsuffixed helper writes no flags.
#define DECLARE_PARTIAL_FLAG_VARIANTS(name, args) \
    void name##_fc args; \
    void name##_fz args; \
    void name##_fn args; \
    void name##_fcz args; \
    void name##_fcn args; \
    void name##_fzn args; \
    void name##_fczn args;

#define DECLARE_PARTIAL_FLAG_VARIANTS_8(name) \
    DECLARE_PARTIAL_FLAG_VARIANTS(name, (uint8_t value))
#define DECLARE_PARTIAL_FLAG_VARIANTS_16(name) \
    DECLARE_PARTIAL_FLAG_VARIANTS(name, (uint16_t value))
#define DECLARE_PARTIAL_FLAG_VARIANTS_0(name) \
    DECLARE_PARTIAL_FLAG_VARIANTS(name, (void))

DECLARE_PARTIAL_FLAG_VARIANTS_8(lda_imm)
DECLARE_PARTIAL_FLAG_VARIANTS_8(lda_zp)
DECLARE_PARTIAL_FLAG_VARIANTS_8(lda_zpx)
DECLARE_PARTIAL_FLAG_VARIANTS_8(lda_zpy)
DECLARE_PARTIAL_FLAG_VARIANTS_8(lda_indy)
DECLARE_PARTIAL_FLAG_VARIANTS_16(lda_abs)
DECLARE_PARTIAL_FLAG_VARIANTS_16(lda_absx)
DECLARE_PARTIAL_FLAG_VARIANTS_16(lda_absy)
DECLARE_PARTIAL_FLAG_VARIANTS_8(ldx_imm)
DECLARE_PARTIAL_FLAG_VARIANTS_8(ldx_zp)
DECLARE_PARTIAL_FLAG_VARIANTS_8(ldx_zpy)
DECLARE_PARTIAL_FLAG_VARIANTS_16(ldx_abs)
DECLARE_PARTIAL_FLAG_VARIANTS_16(ldx_absy)
DECLARE_PARTIAL_FLAG_VARIANTS_8(ldy_imm)
DECLARE_PARTIAL_FLAG_VARIANTS_8(ldy_zp)
DECLARE_PARTIAL_FLAG_VARIANTS_8(ldy_zpx)
DECLARE_PARTIAL_FLAG_VARIANTS_16(ldy_abs)
DECLARE_PARTIAL_FLAG_VARIANTS_16(ldy_absx)
DECLARE_PARTIAL_FLAG_VARIANTS_8(adc_imm)
DECLARE_PARTIAL_FLAG_VARIANTS_8(adc_zp)
DECLARE_PARTIAL_FLAG_VARIANTS_8(adc_zpx)
DECLARE_PARTIAL_FLAG_VARIANTS_8(adc_zpy)
DECLARE_PARTIAL_FLAG_VARIANTS_16(adc_abs)
DECLARE_PARTIAL_FLAG_VARIANTS_16(adc_absx)
DECLARE_PARTIAL_FLAG_VARIANTS_16(adc_absy)
DECLARE_PARTIAL_FLAG_VARIANTS_8(sbc_imm)
DECLARE_PARTIAL_FLAG_VARIANTS_8(sbc_zp)
DECLARE_PARTIAL_FLAG_VARIANTS_8(sbc_zpx)
DECLARE_PARTIAL_FLAG_VARIANTS_16(sbc_abs)
DECLARE_PARTIAL_FLAG_VARIANTS_16(sbc_absx)
DECLARE_PARTIAL_FLAG_VARIANTS_16(sbc_absy)
DECLARE_PARTIAL_FLAG_VARIANTS_8(and_imm)
DECLARE_PARTIAL_FLAG_VARIANTS_8(and_zp)
DECLARE_PARTIAL_FLAG_VARIANTS_16(and_abs)
DECLARE_PARTIAL_FLAG_VARIANTS_16(and_absx)
DECLARE_PARTIAL_FLAG_VARIANTS_16(and_absy)
DECLARE_PARTIAL_FLAG_VARIANTS_8(ora_imm)
DECLARE_PARTIAL_FLAG_VARIANTS_8(ora_zp)
DECLARE_PARTIAL_FLAG_VARIANTS_8(ora_zpx)
DECLARE_PARTIAL_FLAG_VARIANTS_8(ora_zpy)
DECLARE_PARTIAL_FLAG_VARIANTS_16(ora_abs)
DECLARE_PARTIAL_FLAG_VARIANTS_16(ora_absx)
DECLARE_PARTIAL_FLAG_VARIANTS_16(ora_absy)
DECLARE_PARTIAL_FLAG_VARIANTS_8(eor_imm)
DECLARE_PARTIAL_FLAG_VARIANTS_8(eor_zp)
DECLARE_PARTIAL_FLAG_VARIANTS_0(tax)
DECLARE_PARTIAL_FLAG_VARIANTS_0(tay)
DECLARE_PARTIAL_FLAG_VARIANTS_0(tsx)
DECLARE_PARTIAL_FLAG_VARIANTS_0(txa)
DECLARE_PARTIAL_FLAG_VARIANTS_0(tya)
DECLARE_PARTIAL_FLAG_VARIANTS_0(asl_acc)
DECLARE_PARTIAL_FLAG_VARIANTS_16(asl_abs)
DECLARE_PARTIAL_FLAG_VARIANTS_0(lsr_acc)
DECLARE_PARTIAL_FLAG_VARIANTS_8(lsr_zp)
DECLARE_PARTIAL_FLAG_VARIANTS_16(lsr_abs)
DECLARE_PARTIAL_FLAG_VARIANTS_8(inc_zp)
DECLARE_PARTIAL_FLAG_VARIANTS_8(inc_zpx)
DECLARE_PARTIAL_FLAG_VARIANTS_16(inc_abs)
DECLARE_PARTIAL_FLAG_VARIANTS_16(inc_absx)
DECLARE_PARTIAL_FLAG_VARIANTS_0(inx)
DECLARE_PARTIAL_FLAG_VARIANTS_0(iny)
DECLARE_PARTIAL_FLAG_VARIANTS_8(dec_zp)
DECLARE_PARTIAL_FLAG_VARIANTS_8(dec_zpx)
DECLARE_PARTIAL_FLAG_VARIANTS_16(dec_abs)
DECLARE_PARTIAL_FLAG_VARIANTS_16(dec_absx)
DECLARE_PARTIAL_FLAG_VARIANTS_0(dex)
DECLARE_PARTIAL_FLAG_VARIANTS_0(dey)
DECLARE_PARTIAL_FLAG_VARIANTS_8(cmp_imm)
DECLARE_PARTIAL_FLAG_VARIANTS_8(cmp_zp)
DECLARE_PARTIAL_FLAG_VARIANTS_8(cmp_zpx)
DECLARE_PARTIAL_FLAG_VARIANTS_8(cmp_zpy)
DECLARE_PARTIAL_FLAG_VARIANTS_16(cmp_abs)
DECLARE_PARTIAL_FLAG_VARIANTS_16(cmp_absx)
DECLARE_PARTIAL_FLAG_VARIANTS_16(cmp_absy)
DECLARE_PARTIAL_FLAG_VARIANTS_8(cpx_imm)
DECLARE_PARTIAL_FLAG_VARIANTS_8(cpx_zp)
DECLARE_PARTIAL_FLAG_VARIANTS_8(cpy_imm)
DECLARE_PARTIAL_FLAG_VARIANTS_8(cpy_zp)
DECLARE_PARTIAL_FLAG_VARIANTS_16(cpy_abs)
DECLARE_PARTIAL_FLAG_VARIANTS_0(pla)
DECLARE_PARTIAL_FLAG_VARIANTS_8(bit_zp)
DECLARE_PARTIAL_FLAG_VARIANTS_16(bit_abs)
DECLARE_PARTIAL_FLAG_VARIANTS_0(rol_acc)
DECLARE_PARTIAL_FLAG_VARIANTS_8(rol_zp)
DECLARE_PARTIAL_FLAG_VARIANTS_16(rol_abs)
DECLARE_PARTIAL_FLAG_VARIANTS_0(ror_acc)
DECLARE_PARTIAL_FLAG_VARIANTS_16(ror_absx)

#undef DECLARE_PARTIAL_FLAG_VARIANTS_0
#undef DECLARE_PARTIAL_FLAG_VARIANTS_16
#undef DECLARE_PARTIAL_FLAG_VARIANTS_8
#undef DECLARE_PARTIAL_FLAG_VARIANTS

#if defined(__GNUC__)
#pragma GCC visibility pop
#endif

#endif
