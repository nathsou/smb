fn transpile_fixture(source: String) -> Transpiler raise {
  let items = @parse.Parser::new(@parse.Lexer::new(source).tokenize()).parse()
  let program = @image.discover(@image.assemble(items))
  program.validate_stacks()
  let low = @lower.AsmLowering::new(program.source, program=Some(program))
  low.lower()
  transpile_low(low)
}

fn transpile_low(low: @lower.AsmLowering, native~: Array[String] = [], reference~: Bool = false) -> Transpiler raise {
  let program = @ir.build(low, native=Set::from_array(native))
  if reference { program.analyze() |> ignore } else { program.optimize() }
  Transpiler::transpile(low, program, native~)
}

test "dispatch emission uses the 6502 indirect-jump page wrap" {
  let output = transpile_fixture(".org $c000\nBoot:\n ldx #$ff\n txs\n jsr Select\nIdle:\n jmp Idle\nSelect:\n lda #$00\n jsr Choose\n .dw Handler\nHandler:\n rts\nChoose:\n asl\n tay\n pla\n sta $20\n pla\n sta $21\n iny\n lda ($20),y\n sta $ff\n iny\n lda ($20),y\n sta $00\n jmp ($ff)\nTick:\n rti\n.org $fffa\n.dw Tick, Boot, Tick\n")
  let mut code = ""
  for file in output.files {
    if file.name == "code.c" { code = file.contents }
  }
  // JMP ($ff) reads its high byte from $00, not $100
  assert_eq(code.contains("return cpu_dispatch_target = (uint16_t)(ram[0xff] | (ram[0] << 8));"), true)
  assert_eq(code.contains("default: cpu_unresolved_jump(cpu_dispatch_target); return;"), true)
  assert_eq(code.contains("  Select();\n"), true)
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
  let output = transpile_low(lower_fixture(native_fixture), native=["Probe"])
  let files : Map[String, String] = Map([])
  for file in output.files { files[file.name] = file.contents }
  assert_eq(files["code.h"].contains("void Probe(void);"), true)
  assert_eq(files["code.c"].contains("void Probe(void) {"), false)
  assert_eq(files["code.c"].contains("// Probe: native implementation, not generated."), true)
  assert_eq(files["code.c"].contains("// It may read: none."), true)
  assert_eq(files["code.c"].contains("// It must produce: carry."), true)
}

test "unknown native routines are rejected" {
  try transpile_low(lower_fixture(native_fixture), native=["Missing"]) catch {
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
  let code = code_c(transpile_low(lower_fixture(source)))
  assert_eq(code.contains("// in: x; out: a\nvoid Get(void) {"), true)
  assert_eq(code.contains("void Tick(void) {"), true)
  assert_eq(code.contains("// in: a, x, y"), false)
}

test "a BIT hiding a second entry does not read A when its flags are dead" {
  let source = ".org $c000\nBoot:\n jsr Both\n jsr One\n lda #$00\nIdle:\n jmp Idle\nBoth:\n ldy #$00\n .db $2c\nOne:\n ldy #$04\n lda #$f8\n sta $0200,y\n rts\nTick:\n rti\n.org $fffa\n.dw Tick, Boot, Tick\n"
  let code = code_c(transpile_low(lower_fixture(source)))
  assert_eq(code.contains("// in: a"), false)
}

test "instructions become C that computes only live flags" {
  // ADC's carry feeds the BCS; its Z/N/V are dead. LDA's flags are dead.
  let source = ".org $c000\nBoot:\n lda $10\n clc\n adc #$05\n bcs Done\n sta $11\nDone:\n lda #$00\n clc\n clv\nIdle:\n jmp Idle\nTick:\n rti\n.org $fffa\n.dw Tick, Boot, Tick\n"
  let code = code_c(transpile_low(lower_fixture(source)))
  assert_eq(code.contains("uint16_t sum = (uint16_t)(ram[0x10] + 5);\n  a = (uint8_t)sum;\n  if (sum < 0x100) {\n    ram[0x11] = a;\n  }"), true)
  assert_eq(code.contains("zero_flag"), false)
  assert_eq(code.contains("overflow_flag"), false)

}

test "a value with one use moves into it" {
  let source = ".org $c000\nBoot:\n lda $10\n sta $11\n ldx #$03\n stx $12\n stx $13\n lda #$00\n clc\n clv\nIdle:\n jmp Idle\nTick:\n rti\n.org $fffa\n.dw Tick, Boot, Tick\n"
  let code = code_c(transpile_low(lower_fixture(source)))
  assert_eq(code.contains("ram[0x11] = ram[0x10];"), true)
  let unfolded = code_c(transpile_low(lower_fixture(source), reference=true))
  assert_eq(unfolded.contains("a = ram[0x10];\n  zero_flag = a == 0;\n  neg_flag = (a & 0x80) != 0;\n  ram[0x11] = a;"), true)
  // X is a known constant, so the stores use it and X is dead
  assert_eq(code.contains("ram[0x12] = 3;\n  ram[0x13] = 3;"), true)
  assert_eq(code.contains("x = "), false)
}

test "interfaces count only the routine's own callers" {
  // Tail and Other share Tail's RTS; only Other's caller reads A afterwards.
  let source = ".org $c000\nBoot:\n jsr Tail\n jsr Other\n sta $10\n lda #$00\n clc\n clv\nIdle:\n jmp Idle\nOther:\n lda #$05\n jmp Tail\nTail:\n inc $20\n rts\nTick:\n rti\n.org $fffa\n.dw Tick, Boot, Tick\n"
  let code = code_c(transpile_low(lower_fixture(source)))
  assert_eq(code.contains("// in: none; out: a\nvoid Other(void) {"), true)
  assert_eq(code.contains("void Tail(void) {"), true)
  assert_eq(code.contains("out: a\nvoid Tail(void) {"), false)
}

test "read-modify-write outside RAM goes through the bus" {
  let source = ".org $c000\nBoot:\n inc $6000\n asl $6001\n lda #$00\n clc\n clv\nIdle:\n jmp Idle\nTick:\n rti\n.org $fffa\n.dw Tick, Boot, Tick\n"
  let code = code_c(transpile_low(lower_fixture(source)))
  assert_eq(code.contains("dynamic_ram_write(0x6000, (uint8_t)(read_byte(0x6000) + 1));"), true)
  assert_eq(code.contains("dynamic_ram_write(0x6001, (uint8_t)(read_byte(0x6001) << 1));"), true)
}
