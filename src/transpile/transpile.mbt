
pub(all) struct Emitter {
  output: StringBuilder
  mut indentation: Int
  mut jsr_return_index: Int
}

pub fn Emitter::new() -> Emitter {
  {
    output: StringBuilder::new(),
    indentation: 0,
    jsr_return_index: 0,
  }
}

fn Emitter::emit(self: Emitter, s: String, newline~: Bool = true) -> Unit {
  let indent = "  ".repeat(self.indentation)
  self.output.write_string("\{indent}\{s}")

  if newline {
    self.output.write_char('\n')
  }
}

fn hex(n: Int) -> String {
  let mut n = n
  let h = StringBuilder::new()

  if n == 0 {
    return "0"
  }

  while n > 0 {
    let digit = n % 16
    n /= 16

    let d = match digit {
      10 => "a"
      11 => "b"
      12 => "c"
      13 => "d"
      14 => "e"
      15 => "f"
      _ => digit.to_string()
    }

    h.write_string(d)
  }

  h.to_string().rev()
}

fn static_u8(n: Int) -> String {
  "0x\{hex(n & 0xff)}u"
}

fn static_u16(n: Int) -> String {
  "0x\{hex(n & 0xffff)}u"
}

fn static_fallthrough(stmt: String, next: Int) -> String {
  "\{stmt} pc = \{static_u16(next)}; break;"
}

fn static_group_one_address(mode: Int, byte: Int, word: Int) -> String {
  match mode {
    0 => "c6502_indirect_x(\{static_u8(byte)})"
    1 => static_u8(byte)
    3 => static_u16(word)
    4 => "c6502_indirect_y(\{static_u8(byte)})"
    5 => "(uint8_t)(\{static_u8(byte)} + x)"
    6 => "(uint16_t)(\{static_u16(word)} + y)"
    _ => "(uint16_t)(\{static_u16(word)} + x)"
  }
}

fn static_group_two_address(mode: Int, byte: Int, word: Int, index: String) -> String {
  match mode {
    1 => static_u8(byte)
    3 => static_u16(word)
    5 => "(uint8_t)(\{static_u8(byte)} + \{index})"
    _ => "(uint16_t)(\{static_u16(word)} + \{index})"
  }
}

fn static_branch_condition(opcode: Int) -> String {
  match opcode {
    0x10 => "!neg_flag"
    0x30 => "neg_flag"
    0x50 => "!c6502_overflow"
    0x70 => "c6502_overflow"
    0x90 => "!carry_flag"
    0xb0 => "carry_flag"
    0xd0 => "!zero_flag"
    _ => "zero_flag"
  }
}

fn static_group_one(opcode: Int, byte: Int, word: Int, next: Int) -> String {
  let operation = opcode >> 5
  let mode = (opcode >> 2) & 7
  if operation == 4 && mode == 2 {
    return "return;"
  }
  let address = static_group_one_address(mode, byte, word)
  if operation == 4 {
    return static_fallthrough("dynamic_ram_write(\{address}, a);", next)
  }
  let value = if mode == 2 { static_u8(byte) } else { "read_byte(\{address})" }
  let statement = match operation {
    0 => "a |= \{value}; c6502_set_nz(a);"
    1 => "a &= \{value}; c6502_set_nz(a);"
    2 => "a ^= \{value}; c6502_set_nz(a);"
    3 => "c6502_adc(\{value});"
    5 => "a = \{value}; c6502_set_nz(a);"
    6 => "c6502_compare(a, \{value});"
    _ => "c6502_adc((uint8_t)~\{value});"
  }
  static_fallthrough(statement, next)
}

fn static_group_two(opcode: Int, byte: Int, word: Int, next: Int) -> String {
  let operation = opcode >> 5
  let mode = (opcode >> 2) & 7
  if operation <= 3 {
    if mode == 2 {
      return static_fallthrough("a = c6502_shift(a, \{operation}u);", next)
    }
    let address = static_group_two_address(mode, byte, word, "x")
    return match mode {
      1 | 3 | 5 | 7 => static_fallthrough("value = c6502_shift(read_byte(\{address}), \{operation}u); dynamic_ram_write(\{address}, value);", next)
      _ => "return;"
    }
  }
  if operation == 4 { // STX
    let address = static_group_two_address(mode, byte, word, "y")
    return match mode {
      1 | 3 | 5 => static_fallthrough("dynamic_ram_write(\{address}, x);", next)
      _ => "return;"
    }
  }
  if operation == 5 { // LDX
    if mode == 0 {
      return static_fallthrough("x = \{static_u8(byte)}; c6502_set_nz(x);", next)
    }
    let address = static_group_two_address(mode, byte, word, "y")
    return match mode {
      1 | 3 | 5 | 7 => static_fallthrough("x = read_byte(\{address}); c6502_set_nz(x);", next)
      _ => "return;"
    }
  }
  let address = static_group_two_address(mode, byte, word, "x")
  let operation_text = if operation == 6 { "- 1u" } else { "+ 1u" }
  match mode {
    1 | 3 | 5 | 7 => static_fallthrough("value = (uint8_t)(read_byte(\{address}) \{operation_text}); dynamic_ram_write(\{address}, value); c6502_set_nz(value);", next)
    _ => "return;"
  }
}

fn static_group_zero(opcode: Int, address: Int, byte: Int, word: Int, next: Int) -> String {
  if (opcode & 0x1f) == 0x10 {
    let signed = if byte >= 0x80 { byte - 0x100 } else { byte }
    let target = next + signed
    return "if (\{static_branch_condition(opcode)}) { pc = \{static_u16(target)}; } else { pc = \{static_u16(next)}; } break;"
  }
  match opcode {
    0x00 => "c6502_push_word(\{static_u16(next + 1)}); c6502_push(c6502_status(true)); c6502_interrupt_disable = true; pc = read_word(0xfffeu); break;"
    0x08 => static_fallthrough("c6502_push(c6502_status(true));", next)
    0x18 => static_fallthrough("carry_flag = false;", next)
    0x20 => "c6502_push_word(\{static_u16(next - 1)}); pc = \{static_u16(word)}; break;"
    0x24 | 0x2c => {
      let operand = if opcode == 0x24 { "read_byte(\{static_u8(byte)})" } else { "read_byte(\{static_u16(word)})" }
      static_fallthrough("value = \{operand}; zero_flag = (a & value) == 0; neg_flag = (value & 0x80u) != 0; c6502_overflow = (value & 0x40u) != 0;", next)
    }
    0x28 => static_fallthrough("c6502_set_status(c6502_pop());", next)
    0x38 => static_fallthrough("carry_flag = true;", next)
    0x40 => "if (stop_on_rti) return; c6502_set_status(c6502_pop()); pc = c6502_pop_word(); break;"
    0x48 => static_fallthrough("c6502_push(a);", next)
    0x4c => if word == address { "return;" } else { "pc = \{static_u16(word)}; break;" }
    0x58 => static_fallthrough("c6502_interrupt_disable = false;", next)
    0x60 => "pc = (uint16_t)(c6502_pop_word() + 1u); break;"
    0x68 => static_fallthrough("a = c6502_pop(); c6502_set_nz(a);", next)
    0x6c => "pc = c6502_indirect_jmp(\{static_u16(word)}); break;"
    0x78 => static_fallthrough("c6502_interrupt_disable = true;", next)
    0x84 => static_fallthrough("dynamic_ram_write(\{static_u8(byte)}, y);", next)
    0x88 => static_fallthrough("y--; c6502_set_nz(y);", next)
    0x8a => static_fallthrough("a = x; c6502_set_nz(a);", next)
    0x8c => static_fallthrough("dynamic_ram_write(\{static_u16(word)}, y);", next)
    0x94 => static_fallthrough("dynamic_ram_write((uint8_t)(\{static_u8(byte)} + x), y);", next)
    0x98 => static_fallthrough("a = y; c6502_set_nz(a);", next)
    0x9a => static_fallthrough("sp = x;", next)
    0xa0 => static_fallthrough("y = \{static_u8(byte)}; c6502_set_nz(y);", next)
    0xa4 => static_fallthrough("y = read_byte(\{static_u8(byte)}); c6502_set_nz(y);", next)
    0xa8 => static_fallthrough("y = a; c6502_set_nz(y);", next)
    0xac => static_fallthrough("y = read_byte(\{static_u16(word)}); c6502_set_nz(y);", next)
    0xb4 => static_fallthrough("y = read_byte((uint8_t)(\{static_u8(byte)} + x)); c6502_set_nz(y);", next)
    0xb8 => static_fallthrough("c6502_overflow = false;", next)
    0xba => static_fallthrough("x = sp; c6502_set_nz(x);", next)
    0xbc => static_fallthrough("y = read_byte((uint16_t)(\{static_u16(word)} + x)); c6502_set_nz(y);", next)
    0xc0 => static_fallthrough("c6502_compare(y, \{static_u8(byte)});", next)
    0xc4 => static_fallthrough("c6502_compare(y, read_byte(\{static_u8(byte)}));", next)
    0xc8 => static_fallthrough("y++; c6502_set_nz(y);", next)
    0xca => static_fallthrough("x--; c6502_set_nz(x);", next)
    0xcc => static_fallthrough("c6502_compare(y, read_byte(\{static_u16(word)}));", next)
    0xd8 => static_fallthrough("c6502_decimal = false;", next)
    0xe0 => static_fallthrough("c6502_compare(x, \{static_u8(byte)});", next)
    0xe4 => static_fallthrough("c6502_compare(x, read_byte(\{static_u8(byte)}));", next)
    0xe8 => static_fallthrough("x++; c6502_set_nz(x);", next)
    0xea => static_fallthrough("", next)
    0xec => static_fallthrough("c6502_compare(x, read_byte(\{static_u16(word)}));", next)
    0xf8 => static_fallthrough("c6502_decimal = true;", next)
    _ => "return;"
  }
}

fn Emitter::emit_static_case(self: Emitter, image: @layout.Image, inst: @layout.EncodedInstruction) -> Unit {
  let byte = image.read_byte(inst.address + 1).unwrap_or(0)
  let word = byte | (image.read_byte(inst.address + 2).unwrap_or(0) << 8)
  let next = inst.address + inst.size
  let body = match inst.opcode {
    // These implied opcodes occupy cc=2 slots that otherwise describe
    // memory-addressing modes.
    0x8a => static_fallthrough("a = x; c6502_set_nz(a);", next)
    0x9a => static_fallthrough("sp = x;", next)
    0xaa => static_fallthrough("x = a; c6502_set_nz(x);", next)
    0xba => static_fallthrough("x = sp; c6502_set_nz(x);", next)
    0xca => static_fallthrough("x--; c6502_set_nz(x);", next)
    0xea => static_fallthrough("", next)
    _ => match inst.opcode & 3 {
      1 => static_group_one(inst.opcode, byte, word, next)
      2 => static_group_two(inst.opcode, byte, word, next)
      _ => static_group_zero(inst.opcode, inst.address, byte, word, next)
    }
  }
  self.emit("case \{static_u16(inst.address)}: \{body}")
}

fn bin(n: Int) -> String {
  let mut n = n
  let b = StringBuilder::new()

  if n == 0 {
    return "0"
  }

  while n > 0 {
    let digit = n % 2
    n /= 2

    b.write_string(digit.to_string())
  }

  b.to_string().rev().pad_start(8, '0')
}

fn show_const(base~: @parse.ConstantBase, value~: Int) -> String {
  match base {
    Decimal => value.to_string()
    Hexadecimal => "0x\{hex(value)}"
    Binary => "0b\{bin(value)}"
  }
}

fn show_expr(expr: @lower.TypedExpr) -> String {
  match expr {
    Const(base~, value~, size=_, ty=_) => show_const(base~, value~)
    Ident(name~, ty=_) => name
    Unary(op~, expr~, ty=_) => {
      let macro_ = match op {
        GetHighByte => "HIGH_BYTE"
        GetLowByte => "LOW_BYTE"
      }

      "\{macro_}(\{show_expr(expr)})"
    }
    Binary(op~, lhs~, rhs~, ty=_) => {
      let op = match op {
        Add => "+"
        Sub => "-"
      }

      "\{show_expr(lhs)} \{op} \{show_expr(rhs)}"
    }
  }
}

pub(all) suberror EmitterError { EmitterError(String) } derive(Debug)

fn Emitter::emit_constants_h(self: Emitter, low: @lower.AsmLowering) -> Unit {
  self.emit("#ifndef SMB_CONSTANTS_H")
  self.emit("#define SMB_CONSTANTS_H")
  self.emit("")
  self.emit("#define HIGH_BYTE(x) (((x) >> 8) & 0xff)")
  self.emit("#define LOW_BYTE(x) ((x) & 0xff)")
  self.emit("")

  for name, val in low.defines {
    self.emit("#define \{name} \{show_expr(val)}")
  }

  self.emit("\n#endif")
}

fn Emitter::emit_data_h(self: Emitter, low: @lower.AsmLowering) -> Unit {
  self.emit("#ifndef SMB_DATA_H")
  self.emit("#define SMB_DATA_H")
  self.emit("")
  self.emit("#include \"stdint.h\"")
  self.emit("#include \"constants.h\"")
  self.emit("")
  self.emit("#define PRG_ROM_BYTES \{low.image.bytes.length()}")
  self.emit("extern const uint8_t prg_rom[\{low.image.bytes.length()}];")
  self.emit("")

  for label, _table in low.tables_by_label {
    match low.image.symbols.get(label) {
      Some(address) => self.emit("#define \{label} 0x\{hex(address)}")
      None => ()
    }
  }

  self.emit("")
  self.emit("#endif")
}

fn Emitter::emit_data_c(self: Emitter, low: @lower.AsmLowering) -> Unit {
  self.emit("#include \"data.h\"")
  self.emit("")

  self.emit("const uint8_t prg_rom[] = {")
  self.indentation += 1

  for index, byte in low.image.bytes {
    if index % 16 == 0 {
      self.output.write_char('\n')
    }
    self.emit("0x\{hex(byte)},")
  }

  self.indentation -= 1
    self.emit("};\n")
}

fn Emitter::emit_code_h(self: Emitter, low: @lower.AsmLowering) -> Unit {
  self.emit("#ifndef SMB_CODE_H")
  self.emit("#define SMB_CODE_H")
  self.emit("")
  self.emit("#include \"data.h\"")
  self.emit("#include \"instructions.h\"")
  self.emit("#include \"ppu.h\"")
  self.emit("#include \"apu.h\"")
  self.emit("")

  self.emit("void Start(void);")
  self.emit("void NonMaskableInterrupt(void);")

  self.emit("")

  self.emit("#endif")
}

fn rename_label(label: String) -> String {
  label
}

fn gen_jump(target: String, low: @lower.AsmLowering) -> String {
  let renamed_label = rename_label(target)

  if low.targets.subroutines.contains(target) {
    "\{renamed_label}(); return;"
  } else {
    "goto \{renamed_label};"
  }
}

fn show_cond_opcode(op: @parse.OpCode) -> String {
  match op {
    BCC => "!carry_flag"
    BCS => "carry_flag"
    BEQ => "zero_flag"
    BNE => "!zero_flag"
    BMI => "neg_flag"
    BPL => "!neg_flag"
    BVC => "!overflow_flag"
    BVS => "overflow_flag"
    _ => abort("unreachable")
  }
}

priv enum Reg { A; X; Y }
priv struct Range { min: Int; max: Int }

impl Add for Range with add(self: Range, other: Range) -> Range {
  { min: self.min + other.min, max: self.max + other.max }
}

impl Sub for Range with sub(self: Range, other: Range) -> Range {
  { min: self.min - other.min, max: self.max - other.max }
}

fn compute_expr_range(expr: @lower.TypedExpr, low: @lower.AsmLowering) -> Range? {
  match expr {
    Const(value~, ..) => Some({ min: value, max: value })
    Ident(name~, ..) => match low.defines.get(name) {
      Some(expr) => compute_expr_range(expr, low)
      None => None
    }
    Binary(op~, lhs~, rhs~, ..) => {
      let lhs_range = compute_expr_range(lhs, low)
      let rhs_range = compute_expr_range(rhs, low)

      match (lhs_range, rhs_range) {
        (Some(lhs_range), Some(rhs_range)) => match op {
          Add => Some(lhs_range + rhs_range)
          Sub => Some(lhs_range - rhs_range)
        }
        _ => None
      }
    }
    _ => None
  }
}

fn compute_range(expr: @lower.TypedExpr, mode: @lower.AsmAddressingMode, low: @lower.AsmLowering) -> Range? {
  let mode_range = match mode {
    ZeroPageX(_) | AbsoluteX(_) | ZeroPageY(_) | AbsoluteY(_) => { min: 0, max: 0xff }
    _ => { min: 0, max: 0 }
  }

  match compute_expr_range(expr, low) {
    Some(expr_range) => Some(expr_range + mode_range)
    None => None
  }
}

fn write_byte(mode: @lower.AsmAddressingMode, target: Reg, low: @lower.AsmLowering) -> String {
  let target = match target {
    A => "a"
    X => "x"
    Y => "y"
  }

  fn rewrite(addr: @lower.TypedExpr, suffix: String) -> String {
    // Hardware behaviour is selected by the evaluated bus address, not the
    // spelling of a source-level symbol. This also covers renamed NROM
    // disassemblies and indexed register accesses.
    let value = "\{show_expr(addr)}\{suffix}"
    "dynamic_ram_write(\{value}, \{target});"
  }

  match mode {
      ZeroPage(addr) | Absolute(addr) => rewrite(addr, "")
      ZeroPageX(addr) | AbsoluteX(addr) => rewrite(addr, " + x")
      ZeroPageY(addr) | AbsoluteY(addr) => rewrite(addr, " + y")
      IndirectIndexed(addr) => "dynamic_ram_write(read_word(\{show_expr(addr)}) + y, \{target});"
      Implied | Immediate(_) | Relative(_) | Accumulator| Indirect(_) | IndexedIndirect(_) => abort("unreachable")
  }
}

fn Emitter::emit_item(self: Emitter, item: @lower.AsmItem, low: @lower.AsmLowering) -> Unit {
    match item {
      Label(name) => {
        // do not output labels for subroutines
        let is_subroutine_label = low.is_subroutine_label(name);

        if !is_subroutine_label {
          self.emit("\n\{rename_label(name)}:")
        }
      }
      Inst(op~, mode~, comment~) => {
        let stmt = match (op, mode) {
          (TXS, _) => "sp = x;"
          (RTI, _) => "return; // <rti>"
          (CLC, _) => "carry_flag = false;"
          (SEC, _) => "carry_flag = true;"
          (STA, _) => write_byte(mode, A, low)
          (STX, _) => write_byte(mode, X, low)
          (STY, _) => write_byte(mode, Y, low)
          _ => {
            let args = match mode {
              Accumulator => ""
              Implied => ""
              Immediate(expr) => "\{show_expr(expr)}"
              Relative(expr) => "\{show_expr(expr)}"
              ZeroPage(expr) => "\{show_expr(expr)}"
              ZeroPageX(expr) => "\{show_expr(expr)}"
              ZeroPageY(expr) => "\{show_expr(expr)}"
              Absolute(expr) => "\{show_expr(expr)}"
              AbsoluteX(expr) => "\{show_expr(expr)}"
              AbsoluteY(expr) => "\{show_expr(expr)}"
              Indirect(expr) => "\{show_expr(expr)}"
              IndexedIndirect(expr) => "\{show_expr(expr)}"
              IndirectIndexed(expr) => "\{show_expr(expr)}"
            }

            let is_relative = match mode {
              Relative(_) => true
              _ => false
            }

            match (op, is_relative) {
              (BCC | BCS | BEQ | BMI | BNE | BPL | BVC | BVS , true) => {
                let cond = show_cond_opcode(op)

                let target = match mode {
                  Relative(expr) => "\{show_expr(expr)}"
                  _ => abort("unreachable")
                }

                "if (\{cond}) { \{gen_jump(target, low)} }"
              }
              (JMP, _) => {
                gen_jump(args, low)
              }
              (JSR, _) => {
                let target = match mode {
                  Absolute(Ident(name~, ty=_)) => rename_label(name)
                  _ => ""
                }

                "\{target}();"
              }
              (RTS, _) => "return;"
              _ => "\{@debug.to_string(op).to_lower()}\{mode.suffix()}(\{args});"
            }
          }
        }

        let stmt = match comment {
          Some(comment) => {
            "\{stmt} // \{comment}"
          }
          None => stmt
        }

        self.emit(stmt)
      }
      JumpEngine(jump_table~) => {
        self.emit("switch (a) {")
        self.indentation += 1

        for idx, label in jump_table {
          self.emit("case \{idx}: \{gen_jump(label, low)}")
        }

        self.indentation -= 1
        self.emit("}")
      }
      If(cond~, then~, otherwise~, comment~) => {
        let cond = show_cond_opcode(cond)

        match comment {
          Some(comment) => {
            self.emit("// \{comment}")
          }
          None => ()
        }

        self.emit("if (\{cond}) {")

        self.indentation += 1

        for item in then {
          self.emit_item(item, low)
        }

        self.indentation -= 1
        
        match otherwise {
          Some(otherwise) => {
            self.emit("} else {")
            self.indentation += 1

            for item in otherwise {
              self.emit_item(item, low)
            }

            self.indentation -= 1
          }
          None => self.emit("}")
        }
      }
      Comment(comment) => {
        self.emit("// \{comment}")
      }
    }
}

fn Emitter::emit_flat_subroutine(self: Emitter, subroutine: @lower.AsmFlatSubroutine, low: @lower.AsmLowering) -> Unit {
  self.indentation = 0
  self.emit("void \{rename_label(subroutine.name)}(void) {")
  self.indentation = 1

  for item in subroutine.items {
    self.emit_item(item, low)
  }

  self.indentation = 0
  self.emit("}\n")
}

fn Emitter::emit_code_c(self: Emitter, low: @lower.AsmLowering) -> Unit {
  self.emit("#include \"code.h\"")
  self.emit("#include \"recompiler.c\"")
  self.emit("")
  self.emit("static void c6502_run(uint16_t pc, bool stop_on_rti) {")
  self.indentation += 1
  self.emit("uint16_t address;")
  self.emit("uint8_t value;")
  self.emit("for (;;) {")
  self.indentation += 1
  self.emit("switch (pc) {")
  self.indentation += 1
  for inst in low.image.instructions {
    self.emit_static_case(low.image, inst)
  }
  self.emit("default: return;")
  self.indentation -= 1
  self.emit("}")
  self.indentation -= 1
  self.emit("}")
  self.indentation -= 1
  self.emit("}")
  self.emit("")
  self.emit("void Start(void) {")
  self.indentation += 1
  self.emit("c6502_overflow = false;")
  self.emit("c6502_interrupt_disable = true;")
  self.emit("c6502_decimal = false;")
  self.emit("c6502_run(read_word(0xfffcu), false);")
  self.indentation -= 1
  self.emit("}")
  self.emit("")
  self.emit("void NonMaskableInterrupt(void) {")
  self.indentation += 1
  self.emit("c6502_run(read_word(0xfffau), true);")
  self.indentation -= 1
  self.emit("}")
}

pub(all) struct File {
  name: String
  contents: String
  is_lib: Bool
}

pub fn File::write(self: File, dir~: String) -> Unit raise Error {
  @fs.write_string_to_file("\{dir}/\{self.name}", self.contents)
}

pub(all) struct Transpiler {
  files: Array[File]
}

pub fn Transpiler::transpile(low: @lower.AsmLowering) -> Transpiler {
  let constants_h: File = {
    name: "constants.h",
    contents: {
      let emitter = Emitter::new()
      emitter.emit_constants_h(low)
      emitter.output.to_string()
    },
    is_lib: true,
  }

  let data_h = {
    name: "data.h",
    contents: {
      let emitter = Emitter::new()
      emitter.emit_data_h(low)
      emitter.output.to_string()
    },
    is_lib: true,
  }

  let data_c = {
    name: "data.c",
    contents: {
      let emitter = Emitter::new()
      emitter.emit_data_c(low)
      emitter.output.to_string()
    },
    is_lib: true,
  }

  let code_h = {
    name: "code.h",
    contents: {
      let emitter = Emitter::new()
      emitter.emit_code_h(low)
      emitter.output.to_string()
    },
    is_lib: true,
  }

  let code_c = {
    name: "code.c",
    contents: {
      let emitter = Emitter::new()
      emitter.emit_code_c(low)
      emitter.output.to_string()
    },
    is_lib: true,
  }

  {
    files: [
      constants_h,
      data_h, data_c,
      code_h, code_c,
    ],
  }
}
