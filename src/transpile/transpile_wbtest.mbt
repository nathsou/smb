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
  assert_eq(code.contains("return cpu_dispatch_target = read_indirect_word(0xff);"), true)
  assert_eq(code.contains("default: cpu_unresolved_jump(cpu_dispatch_target); return;"), true)
  assert_eq(code.contains("CALL(Select, 0xc005);"), true)
}


fn lower_fixture(source: String) -> @lower.AsmLowering raise {
  let items = @parse.Parser::new(@parse.Lexer::new(source).tokenize()).parse()
  let program = @image.discover(@image.assemble(items))
  program.validate_stacks()
  let low = @lower.AsmLowering::new(program.source, program=Some(program))
  low.lower()
  low
}

let native_fixture : String = ".org $c000\nBoot:\n jsr Probe\n bcs Boot\n clc\n clv\n lda #$00\nIdle:\n jmp Idle\nProbe:\n lda $2002\n asl\n rts\nTick:\n rti\n.org $fffa\n.dw Tick, Boot, Tick\n"

test "native routines keep their prototype and document flag demands" {
  let output = Transpiler::transpile(lower_fixture(native_fixture), native=["Probe"])
  let files : Map[String, String] = Map([])
  for file in output.files { files[file.name] = file.contents }
  assert_eq(files["code.h"].contains("void Probe(void);"), true)
  assert_eq(files["code.c"].contains("void Probe(void) {"), false)
  assert_eq(files["code.c"].contains("// Probe: native implementation, not generated."), true)
  assert_eq(files["code.c"].contains("// It may read: none."), true)
  assert_eq(files["code.c"].contains("// It must produce: carry."), true)
}

test "unknown native routines are rejected" {
  try Transpiler::transpile(lower_fixture(native_fixture), native=["Missing"]) catch {
    UnknownNativeRoutine(name) => assert_eq(name, "Missing")
    _ => fail("unexpected error")
  } noraise {
    _ => fail("expected an unknown native routine error")
  }
}

fn code_c(output: Transpiler) -> String {
  for file in output.files {
    if file.name == "code.c" { return file.contents }
  }
  ""
}

test "routine interfaces list registers read on entry and consumed outputs" {
  // Get reads X and produces A; Y only passes through Get, so it is neither
  // an input nor an output of Get even though Boot reads it afterwards.
  let source = ".org $c000\nBoot:\n ldx #$01\n ldy #$02\n jsr Get\n sta $10\n sty $11\n lda #$00\nIdle:\n jmp Idle\nGet:\n lda $20,x\n rts\nTick:\n rti\n.org $fffa\n.dw Tick, Boot, Tick\n"
  let code = code_c(Transpiler::transpile(lower_fixture(source)))
  assert_eq(code.contains("// in: x; out: a\nvoid Get(void) {"), true)
  assert_eq(code.contains("void Tick(void) {"), true)
  assert_eq(code.contains("// in: a, x, y"), false)
}

test "a BIT hiding a second entry does not read A when its flags are dead" {
  let source = ".org $c000\nBoot:\n jsr Both\n jsr One\n lda #$00\nIdle:\n jmp Idle\nBoth:\n ldy #$00\n .db $2c\nOne:\n ldy #$04\n lda #$f8\n sta $0200,y\n rts\nTick:\n rti\n.org $fffa\n.dw Tick, Boot, Tick\n"
  let code = code_c(Transpiler::transpile(lower_fixture(source)))
  assert_eq(code.contains("// in: a"), false)
}
