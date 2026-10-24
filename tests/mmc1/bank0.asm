; Bank 0 of tests/mmc1: its setup, step and tick.
SwitchBank = $c000
WaitFrame = $c003
Choose = $c006
WriteControl = $c00c
.org $8000
    jmp Setup               ; $8000
    jmp Step                ; $8003

Setup:
    ldx #$00
SetupLoop:
    lda Pattern,x           ; this bank's bytes
    sta $6000,x             ; the cartridge's RAM
    inx
    cpx #$04
    bne SetupLoop
    lda $2002
    lda #$00
    sta $2006
    lda #$10
    sta $2006               ; tile 1 of the pattern tables (CHR RAM)
    ldx #$00
TileLoop:
    lda $6000,x
    sta $2007
    inx
    cpx #$04
    bne TileLoop
    rts

Step:
    inc $30
    lda $30
    and #$01
    jsr Choose
    .dw Even, Odd
Even:
    lda $6001
    sta $31
    rts
Odd:
    inc $32
    lda $32
    and #$01
    ora #$0e                ; vertical or horizontal mirroring, in turn
    jmp WriteControl

Pattern:
    .db $11, $22, $33, $44

.org $b000
Tick:
    inc $40
    lda TickValue
    sta $41
    rts
TickValue:
    .db $a0
