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

fn transpile_rom(source: String, map_text: String) -> Map[String, String] raise {
  let items = @parse.Parser::new(@parse.Lexer::new(source).tokenize()).parse()
  let image = @image.assemble(items)
  let map = @image.parse_map(map_text)
  let program = @image.discover_rom(@image.rom_image({ prg: image.bytes, chr: [] }, map), map)
  program.validate_stacks()
  let low = @lower.AsmLowering::new(program.source, program=Some(program))
  low.lower()
  let files : Map[String, String] = Map([])
  for file in transpile_low(low).files { files[file.name] = file.contents }
  files
}

test "map names read and write memory as views" {
  let files = transpile_rom(
    ".org $c000\nBoot:\n ldx $20\n ldy $21\n lda $10,x\n sta $0300\n lda $c040,x\n sta $07f0,x\n lda $0302,y\n sta $40\n pha\n pla\n sta $0301\nIdle:\n jmp Idle\nTick:\n rti\n.org $c040\n.db 1, 2, 3, 4\n.org $fffa\n.dw Tick, Boot, Tick\n",
    "routine $c000 boot\nroutine $c01c tick\nvar $0010 enemy_y 6\nvar $0020 slot\nvar $0021 row\nvar $0300 score\nvar $0301 list 8\nvar $0302 list_second\nvar $07f0 tail 16 ; near the end of RAM\nvar $0040 boot_flag\nvar $0041 tick 2\ntable $c040 steps 4\n",
  )
  let code = files["code.c"]
  let vars = files["vars.h"]
  // page zero wraps, the end of RAM mirrors to its start
  assert_eq(vars.contains("#define enemy_y(i) ram[(uint8_t)(ENEMY_Y + (i))]"), true)
  assert_eq(vars.contains("#define tail(i) ram[(TAIL + (i)) & 0x7ff] // near the end of RAM"), true)
  assert_eq(vars.contains("#define score ram[SCORE]"), true)
  assert_eq(vars.contains("#define list (&ram[LIST])"), true)
  assert_eq(vars.contains("#define steps (&ROM(STEPS))"), true)
  assert_eq(code.contains("score = enemy_y(x);"), true)
  assert_eq(code.contains("tail(x) = steps[x];"), true)
  // a scalar read with an index is the array holding it
  assert_eq(code.contains("x = slot;\n  y = row;"), true)
  assert_eq(code.contains("a = list[1 + y];\n  boot_flag = a;"), true)
  assert_eq(code.contains("stack[sp] = a;"), true)
  // a name the generated code already uses (a routine) is no view
  assert_eq(vars.contains("#define tick"), false)
  assert_eq(code.contains("#include \"vars.h\""), true)
}

test "assembly input keeps addresses" {
  let output = transpile_fixture(".org $c000\nBoot:\n lda $10\n sta $0300\nIdle:\n jmp Idle\nTick:\n rti\n.org $fffa\n.dw Tick, Boot, Tick\n")
  for file in output.files { assert_eq(file.name == "vars.h", false) }
  assert_eq(code_c(output).contains("#include \"vars.h\""), false)
}

test "constants beside typed variables are written by name" {
  let files = transpile_rom(
    ".org $c000\nBoot:\n lda $06fc\n and #$c0\n beq Skip\n lda #$01\n sta $0770\nSkip:\n lda $06fc\n and #$7f\n sta $0300\n lda $0770\n cmp #$03\n bne Boot\nIdle:\n jmp Idle\nTick:\n rti\n.org $fffa\n.dw Tick, Boot, Tick\n",
    "enum game_mode title=0 playing=1 victory=2 game_over=3\nflags button a=$80 b=$40 select=$20 start=$10 up=$08 down=$04 left=$02 right=$01\nroutine $c000 boot\nvar $06fc joypad1 : button\nvar $0770 game_mode : game_mode\nvar $0300 kept\n",
  )
  let code = files["code.c"]
  let vars = files["vars.h"]
  assert_eq(vars.contains("enum {\n  GAME_MODE_TITLE = 0,\n  GAME_MODE_PLAYING = 1,"), true)
  assert_eq(vars.contains("  BUTTON_A = 0x80,\n"), true)
  assert_eq(code.contains("(joypad1 & (BUTTON_A | BUTTON_B)) != 0"), true)
  assert_eq(code.contains("game_mode = GAME_MODE_PLAYING;"), true)
  // a mask with most bits set names those it clears
  assert_eq(code.contains("kept = joypad1 & ~BUTTON_A;"), true)
  assert_eq(code.contains("game_mode != GAME_MODE_GAME_OVER"), true)
}
