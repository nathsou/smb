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

test "single-entry loops become do-while regions" {
  let low = lower_fixture("Root__sub:\n ldx #$03\nLoop:\n dex\n bne Loop\n rts\n")
  assert_eq(low.flat_subroutines[0].items.iter().any(fn(item) { item is DoWhile(_) }), true)
}

test "a crossing entry prevents unsafe conditional folding" {
  let low = lower_fixture("Root__sub:\n beq Inside\n bne End\nInside:\n lda #$01\nEnd:\n sta $00\n rts\n")
  fn retained(items: Array[AsmItem]) -> Bool {
    items.iter().any(fn(item) {
      match item {
        Inst(op=BNE, mode=Relative(Ident(name="End", ..)), ..) => true
        If(then~, otherwise~, ..) => retained(then) || (match otherwise { Some(other) => retained(other); None => false })
        _ => false
      }
    })
  }
  assert_eq(retained(low.flat_subroutines[0].items), true)
}

test "overflow branches can be structured and inverted" {
  let low = lower_fixture("Root__sub:\n bvs Done\n lda #$01\nDone:\n sta $00\n rts\n")
  assert_eq(low.flat_subroutines[0].items.iter().any(fn(item) { item is If(cond=BVC, ..) }), true)
}
