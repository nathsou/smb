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

test "instructions become C that updates only live flags" {
  // ADC's carry feeds the BCS; its Z/N/V are dead. LDA's flags are dead.
  let source = ".org $c000\nBoot:\n lda $10\n clc\n adc #$05\n bcs Done\n sta $11\nDone:\n lda #$00\n clc\n clv\nIdle:\n jmp Idle\nTick:\n rti\n.org $fffa\n.dw Tick, Boot, Tick\n"
  let code = code_c(Transpiler::transpile(lower_fixture(source)))
  assert_eq(code.contains("a = ram[0x10];\n"), true)
  assert_eq(code.contains("{ uint16_t sum = a + 0x5 + carry_flag; carry_flag = sum > 0xff; a = (uint8_t)sum; }"), true)
  assert_eq(code.contains("adc_imm"), false)

}

test "a load and store of a dead register become one assignment" {
  let source = ".org $c000\nBoot:\n lda $10\n sta $11\n ldx #$03\n stx $12\n stx $13\n lda #$00\n clc\n clv\nIdle:\n jmp Idle\nTick:\n rti\n.org $fffa\n.dw Tick, Boot, Tick\n"
  let code = code_c(Transpiler::transpile(lower_fixture(source)))
  assert_eq(code.contains("ram[0x11] = ram[0x10];"), true)
  let unfolded = code_c(Transpiler::transpile(lower_fixture(source), fold=false))
  assert_eq(unfolded.contains("a = ram[0x10];\n  ram[0x11] = a;"), true)
  // X is still read by the second store, so the first pair is kept
  assert_eq(code.contains("x = 0x3;\n  ram[0x12] = x;\n  ram[0x13] = x;"), true)
}

test "interfaces count only the routine's own callers" {
  // Tail and Other share Tail's RTS; only Other's caller reads A afterwards.
  let source = ".org $c000\nBoot:\n jsr Tail\n jsr Other\n sta $10\n lda #$00\n clc\n clv\nIdle:\n jmp Idle\nOther:\n lda #$05\n jmp Tail\nTail:\n inc $20\n rts\nTick:\n rti\n.org $fffa\n.dw Tick, Boot, Tick\n"
  let code = code_c(Transpiler::transpile(lower_fixture(source)))
  assert_eq(code.contains("// in: none; out: a\nvoid Other(void) {"), true)
  assert_eq(code.contains("void Tail(void) {"), true)
  assert_eq(code.contains("out: a\nvoid Tail(void) {"), false)
}

test "read-modify-write outside RAM goes through the bus" {
  let source = ".org $c000\nBoot:\n inc $6000\n asl $6001\n lda #$00\n clc\n clv\nIdle:\n jmp Idle\nTick:\n rti\n.org $fffa\n.dw Tick, Boot, Tick\n"
  let code = code_c(Transpiler::transpile(lower_fixture(source)))
  assert_eq(code.contains("{ uint16_t address = 0x6000; uint8_t m = read_byte(address); m++; dynamic_ram_write(address, m); }"), true)
  assert_eq(code.contains("{ uint16_t address = 0x6001; uint8_t m = read_byte(address); m = (uint8_t)(m << 1); dynamic_ram_write(address, m); }"), true)
}
