; Independently authored NROM-128 regression program. No SMB names or hints.
OamAddress = $2003
.org $c000
Boot:
    sei
    ldx #$ff
    txs
    jsr Fill
    jsr SelectHandler
    lda #$21
    sta OamAddress
    ldx #$01
    lda #$42
    sta $2003,x             ; indexed hardware address must retain X
    lda #$11
    sta $ff
    lda #$00
    sta $00
    ldy #$02
    lda #$05
    sta ($ff),y             ; zero-page pointer high byte wraps to $00
    ldx #$01
    lda #$77
    sta $ff,x               ; indexed zero-page destination wraps to $00
    jsr RawEntry
    jsr Alternate
Idle:
    jmp Idle

Fill:
    ldx #$03
FillLoop:
    txa
    sta $07ff,x             ; absolute indexed RAM mirrors through $0800
    dex
    bpl FillLoop
    rts

SelectHandler:
    lda #$01
    jsr Choose
    .dw First, Second
First:
    inc $10
    rts
Second:
    inc $11
    rts

Choose:
    asl
    tay
    pla
    sta $20
    pla
    sta $21
    iny
    lda ($20),y
    sta $22
    iny
    lda ($20),y
    sta $23
    jmp ($22)

RawEntry:
    ; LDA #$08; BIT $04A0 overlaps Alternate's LDY #$04.
    .db $a9, $08, $2c
Alternate:
    ldy #$04
    sta $12
    rts

Tick:
    inc $14
    rti

.org $fffa
    .dw Tick, Boot, Tick
