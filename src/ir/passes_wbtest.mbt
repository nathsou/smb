fn program_of(source : String) -> Program raise {
  let items = @parse.Parser::new(@parse.Lexer::new(source).tokenize()).parse()
  let image = @image.assemble(items)
  let discovered = @image.discover(image)
  discovered.validate_stacks()
  let low = @lower.AsmLowering::new(discovered.source, program=Some(discovered))
  low.lower()
  build(low, native=Set([]))
}

fn stmts_of(p : Program, name : String) -> Array[Stmt] {
  let out = []
  for block in p.function(name).unwrap().blocks {
    for s in block.stmts { out.push(s) }
  }
  out
}

test "constants propagate across blocks and ROM reads at known addresses fold" {
  // Y is 2 on both paths into Join, so the table read is the byte $33.
  let p = program_of(
    ".org $c000\nBoot:\n ldy #$02\n lda $10\n beq Join\n ldy #$02\nJoin:\n lda Table,y\n sta $20\nIdle:\n jmp Idle\nTable:\n .db $11, $22, $33\nTick:\n rti\n.org $fffa\n.dw Tick, Boot, Tick\n",
  )
  p.optimize()
  let stmts = stmts_of(p, "Boot")
  assert_eq(stmts.contains(Store(Ram, Const(0x20), Const(0x33))), true)
  assert_eq(stmts.iter().any(fn(s) { s is Assign(Y, _) }), false)
}

test "small leaf routines are inlined" {
  let p = program_of(
    ".org $c000\nBoot:\n jsr Leaf\n jsr Leaf\nIdle:\n jmp Idle\nLeaf:\n inc $10\n rts\nTick:\n rti\n.org $fffa\n.dw Tick, Boot, Tick\n",
  )
  p.optimize()
  assert_eq(p.inline_leaves(), 2)
  p.optimize()
  let stmts = stmts_of(p, "Boot")
  assert_eq(stmts.iter().any(fn(s) { s is Call(_) }), false)
  assert_eq(stmts.length(), 2)
}
