; Independently authored MMC1 regression program: the fixed bank, at $C000
; (tests/mmc1/bank*.asm are the switchable banks). No game names or hints.
; The banks call the fixed bank through the jump table at its start.
.org $c000
    jmp SwitchBank          ; $c000: map bank A at $8000
    jmp WaitFrame           ; $c003
    jmp Choose              ; $c006: jump through the table after the call
    jmp Abort               ; $c009: return from the caller too
    jmp WriteControl        ; $c00c

Reset:
    sei
    cld
    ldx #$ff
    txs
    lda #$80
    sta $8000               ; reset the shift register
    lda #$0e                ; vertical mirroring, the last bank at $c000
    jsr WriteControl
    lda #$00
    jsr SwitchBank
    jsr $8000               ; the window: bank 0's setup
    lda #$90
    sta $2000
    lda #$00
    sta $11
Main:
    jsr $8003               ; the window: the step of bank 0 or 1
    jsr WaitFrame           ; waits in a subroutine
    inc $11
    lda $11
    and #$01
    jsr SwitchBank
    lda $11
    and #$03
    bne Main
    ; every fourth frame, copy bank 2's bytes through the window
    lda #$02
    jsr SwitchBank
    ldy #$03
Copy:
    lda $9000,y
    sta $0300,y
    dey
    bpl Copy
    lda #$00
    jsr SwitchBank
    jmp Main

WaitFrame:
    lda #$00
    sta $12
Wait:
    lda $12
    beq Wait
    rts

SwitchBank:
    sta $13
    sta $e000
    lsr
    sta $e000
    lsr
    sta $e000
    lsr
    sta $e000
    lsr
    sta $e000
    lda $13
    rts

WriteControl:
    sta $8000
    lsr
    sta $8000
    lsr
    sta $8000
    lsr
    sta $8000
    lsr
    sta $8000
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
    jmp ($0022)

Abort:
    inc $16
    pla
    pla
    rts

Nmi:
    pha
    txa
    pha
    tya
    pha
    inc $12
    inc $14
    jsr $b000               ; the window: the tick of bank 0 or 1
    pla
    tay
    pla
    tax
    pla
    rti

.org $fffa
.dw Nmi, Reset, Reset
