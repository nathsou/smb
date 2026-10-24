; Bank 1 of tests/mmc1: its step and tick, with computed jumps and a call
; that returns past its caller.
Abort = $c009
.org $8003
    jmp Step

Step:
    inc $50
    lda $50                 ; through a pointer in RAM to a code address
    and #$01
    asl
    tax
    lda Handlers,x
    sta $24
    lda Handlers+1,x
    sta $25
    jsr Indirect
    lda $50                 ; returning through a pushed address
    and #$01
    asl
    tax
    jsr Pushed
    jsr Caller
    inc $53
    rts

Indirect:
    jmp ($0024)
HandlerA:
    inc $51
    rts
HandlerB:
    inc $52
    rts

Pushed:
    lda Moves+1,x
    pha
    lda Moves,x
    pha
    rts
MoveA:
    inc $54
    rts
MoveB:
    inc $55
    rts

Caller:
    inc $56
    jsr Abort               ; returns to Step, past the rest of Caller

Handlers:
    .dw HandlerA, HandlerB
Moves:
    .dw MoveA - 1, MoveB - 1

.org $b000
Tick:
    inc $42
    lda TickValue
    sta $43
    rts
TickValue:
    .db $b1
