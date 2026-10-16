; Instruction semantics fixture: each routine applies one operation to $00
; (and $01) and saves the result to $02 and the status byte to $03. PHP makes
; every flag live, so the generated C computes all of them. tests/semantics.c
; calls the routines directly and checks them against independent models.
.org $c000
Boot:
    sei
    ldx #$ff
    txs
    jsr Add
    jsr Subtract
    jsr Compare
    jsr BitTest
    jsr RotateLeft
    jsr RotateRight
    jsr ShiftMemory
    jsr RotateMemory
    jsr IncrementIndexed
    jsr ShiftBus
Idle:
    jmp Idle

Add:
    lda $00
    adc $01
    sta $02
    php
    pla
    sta $03
    rts

Subtract:
    lda $00
    sbc $01
    sta $02
    php
    pla
    sta $03
    rts

Compare:
    lda $00
    cmp $01
    php
    pla
    sta $03
    rts

BitTest:
    lda $00
    bit $01
    php
    pla
    sta $03
    rts

RotateLeft:
    lda $00
    rol
    sta $02
    php
    pla
    sta $03
    rts

RotateRight:
    lda $00
    ror
    sta $02
    php
    pla
    sta $03
    rts

ShiftMemory:
    asl $00
    php
    pla
    sta $03
    rts

RotateMemory:
    ror $00
    php
    pla
    sta $03
    rts

; zero-page indexing wraps within page zero
IncrementIndexed:
    ldx #$f0
    inc $11,x
    php
    pla
    sta $03
    rts

; read-modify-write outside RAM goes through the bus (open bus reads 0)
ShiftBus:
    lsr $6000
    php
    pla
    sta $03
    rts

Tick:
    rti

.org $fffa
    .dw Tick, Boot, Tick
