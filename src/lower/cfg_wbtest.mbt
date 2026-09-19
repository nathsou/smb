fn lower_fixture(source: String) -> AsmLowering raise {
  let items = @parse.Parser::new(@parse.Lexer::new(source).tokenize()).parse()
  let result = AsmLowering::new(items)
  result.lower()
  result
}

test "the final instruction and shared returns survive region recovery" {
  let low = lower_fixture("Entry__sub:\n lda #$01\n beq Shared\n jsr Other\n rts\nOther:\n lda #$02\nShared:\n rts\n")
  assert_eq(low.subroutines.length(), 2)
  assert_eq(low.basic_blocks.last().unwrap().items.last().unwrap() is Inst(op=RTS, ..), true)
  let mut found_return = false
  for item in low.lowered {
    if item is Inst(mode=Relative(Ident(name="__ret__", ..)), ..) { found_return = true }
  }
  assert_eq(found_return, true)
}

test "backward shared-entry edges stay in the same host frame" {
  let low = lower_fixture("Root__sub:\n jsr First\n jsr Second\n rts\nFirst:\n lda #$01\nSecond:\n dex\n bne First\n rts\n")
  for sub in low.subroutines {
    if sub.name == "First" || sub.name == "Second" {
      assert_eq(sub.internal_labels.contains("First"), true)
      assert_eq(sub.internal_labels.contains("Second"), true)
    }
  }
}

test "acyclic tail entry is outlined without absorbing unrelated code" {
  let low = lower_fixture("Root__sub:\n jsr First\n jsr Tail\n rts\nFirst:\n lda #$01\n jmp Tail\nUnreachable:\n lda #$ff\n rts\nTail:\n sta $00\n rts\n")
  for sub in low.subroutines {
    if sub.name == "First" {
      assert_eq(sub.internal_labels.contains("Tail"), false)
      assert_eq(sub.internal_labels.contains("Unreachable"), false)
    }
  }
}
