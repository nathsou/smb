fn fixture(s: String) -> Image raise {
  assemble(@parse.Parser::new(@parse.Lexer::new(s).tokenize()).parse())
}

test "aliases, mixed data, forward expressions and origin gaps" {
  let image = fixture(".org $8000\nAlias:\nData:\n.db <Later, >Later\n.dw Later\n.org $8010\nLater:\n lda #$04\n rts\n")
  assert_eq(image.symbols["Alias"], image.symbols["Data"])
  assert_eq(image.word(0x8000), 0x8010)
  assert_eq(image.word(0x8002), 0x8010)
  assert_eq(image.read(0x800f), 0)
  assert_eq(image.read(0x8010), 0xa9)
}

test "an unavailable zero-page indexed form selects the absolute encoding" {
  let image = fixture(".org $8000\n cmp $10,y\nNext:\n rts\n")
  assert_eq(image.read(0x8000), 0xd9)
  assert_eq(image.symbols["Next"], 0x8003)
}

test "relative branch displacement uses the following instruction address" {
  let image = fixture(".org $8000\nLoop:\n dex\n bne Loop\n rts\n")
  assert_eq(image.read(0x8002), 0xfd)
}

test "all documented opcode encodings round trip" {
  let mut count = 0
  for i in 0..<256 {
    if decoding(i) is Some((op, mode)) {
      assert_eq(encoding(op, mode), Some(i))
      count += 1
    }
  }
  assert_eq(count, 151)
  assert_eq(decoding(0x02), None)
}

test "renamed dispatch with different scratch addresses is proven by execution" {
  let image = fixture(".org $8000\nBoot:\n lda #$01\n jsr Choose\n .dw First, Second\nFirst:\n rts\nSecond:\n rts\nChoose:\n asl\n tay\n pla\n sta $20\n pla\n sta $21\n iny\n lda ($20),y\n sta $22\n iny\n lda ($20),y\n sta $23\n jmp ($22)\nNmi:\n rti\n.org $fffa\n.dw Nmi, Boot, Nmi\n")
  let p = discover(image)
  assert_eq(p.dispatches.length(), 1)
  let dispatch = p.dispatches[0x8002]
  assert_eq(dispatch.pointer, 0x22)
  assert_eq(dispatch.targets[1], image.symbols["Second"])
  assert_eq(p.instructions.contains(image.symbols["Choose"]), false)
  assert_eq(dispatch.setup.length(), 12)
}

test "constant branch does not decode adjacent padding" {
  let image = fixture(".org $8000\nBoot:\n lda #$01\n bne Exit\n .db $02\nExit:\n rts\nNmi:\n rti\n.org $fffa\n.dw Nmi, Boot, Nmi\n")
  let p = discover(image)
  assert_eq(p.instructions.contains(0x8004), false)
  assert_eq(condition(@parse.BNE, p.facts[0x8002]), Some(true))
}

test "overlapping BIT and immediate-load entries both survive" {
  let image = fixture(".org $8000\nBoot:\n jsr First\n jsr Second\n rts\nFirst:\n ldy #$00\n .db $2c\nSecond:\n ldy #$04\n rts\nNmi:\n rti\n.org $fffa\n.dw Nmi, Boot, Nmi\n")
  let p = discover(image)
  assert_eq(p.instructions[0x8009].op, @parse.BIT)
  assert_eq(p.instructions[0x800a].op, @parse.LDY)
  assert_eq(p.instructions[0x8009].arg, 0x04a0)
}

test "unknown branch facts are not mistaken for false" {
  let image = fixture(".org $8000\nBoot:\n lda $00\n beq Left\n lda #$01\n jmp Join\nLeft:\n lda #$02\nJoin:\n cmp #$01\n bne Exit\n nop\nExit:\n rts\nNmi:\n rti\n.org $fffa\n.dw Nmi, Boot, Nmi\n")
  let p = discover(image)
  assert_eq(p.facts[image.symbols["Join"]].a, None)
}

test "complementary branches preserve the stack path condition" {
  let p = discover(fixture(".org $8000\nBoot:\n pha\n pla\n bcc Exit\n bcs Exit\n pla\nExit:\n rts\nNmi:\n rti\n.org $fffa\n.dw Nmi, Boot, Nmi\n"))
  p.validate_stacks()
}

test "unrecognized return-token manipulation is rejected" {
  let p = discover(fixture(".org $8000\nBoot:\n jsr Broken\n rts\nBroken:\n pla\n rts\nNmi:\n rti\n.org $fffa\n.dw Nmi, Boot, Nmi\n"))
  let rejected = try { p.validate_stacks(); false } catch { _ => true }
  assert_eq(rejected, true)
}

test "a byte directive can contain several reachable instructions" {
  let p = discover(fixture(".org $8000\nBoot:\n jsr Raw\n rts\nRaw:\n.db $a9, $02, $85, $01, $60\nNmi:\n rti\n.org $fffa\n.dw Nmi, Boot, Nmi\n"))
  assert_eq(p.instructions[0x8004].op, @parse.LDA)
  assert_eq(p.instructions[0x8006].op, @parse.STA)
  assert_eq(p.instructions[0x8008].op, @parse.RTS)
  p.validate_stacks()
}

test "immutable indirect jumps preserve the 6502 page-wrap behavior" {
  let p = discover(fixture(".org $8000\n.db $82\nBoot:\n jmp ($80ff)\n.org $80ff\n.db $20\n.org $8100\n.db $99\n.org $8220\nTarget:\n rts\nNmi:\n rti\n.org $fffa\n.dw Nmi, Boot, Nmi\n"))
  assert_eq(p.instructions.contains(0x8220), true)
  p.validate_stacks()
}

test "ordinary callees cannot execute an interrupt return" {
  let p = discover(fixture(".org $8000\nBoot:\n jsr Broken\n rts\nBroken:\n rti\nNmi:\n rti\n.org $fffa\n.dw Nmi, Boot, Nmi\n"))
  let rejected = try { p.validate_stacks(); false } catch { _ => true }
  assert_eq(rejected, true)
}

test "unknown indirect destinations fail closed" {
  let rejected = try {
    let _ = discover(fixture(".org $8000\nBoot:\n jmp ($20)\nNmi:\n rti\n.org $fffa\n.dw Nmi, Boot, Nmi\n"))
    false
  } catch { _ => true }
  assert_eq(rejected, true)
}
