fn transpile_fixture(source: String) -> Transpiler raise {
  let items = @parse.Parser::new(@parse.Lexer::new(source).tokenize()).parse()
  let program = @image.discover(@image.assemble(items))
  program.validate_stacks()
  let low = @lower.AsmLowering::new(program.source, program=Some(program))
  low.lower()
  Transpiler::transpile(low)
}

test "dispatch emission uses the 6502 indirect-jump page wrap" {
  let output = transpile_fixture(".org $c000\nBoot:\n ldx #$ff\n txs\n jsr Select\nIdle:\n jmp Idle\nSelect:\n lda #$00\n jsr Choose\n .dw Handler\nHandler:\n rts\nChoose:\n asl\n tay\n pla\n sta $20\n pla\n sta $21\n iny\n lda ($20),y\n sta $ff\n iny\n lda ($20),y\n sta $00\n jmp ($ff)\nTick:\n rti\n.org $fffa\n.dw Tick, Boot, Tick\n")
  let mut code = ""
  for file in output.files {
    if file.name == "code.c" { code = file.contents }
  }
  assert_eq(code.contains("return read_indirect_word(0xff);"), true)
  assert_eq(code.contains("cpu_unresolved_jump(read_indirect_word(0xff))"), true)
}
