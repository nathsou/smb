
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

fn emit(self: Emitter, s: String, newline~: Bool = true) -> Unit {
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

pub(all) type! EmitterError String derive(Show)

fn emit_constants_h(self: Emitter, low: @lower.AsmLowering) -> Unit {
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

fn emit_data_h(self: Emitter, low: @lower.AsmLowering) -> Unit {
  self.emit("#ifndef SMB_DATA_H")
  self.emit("#define SMB_DATA_H")
  self.emit("")
  self.emit("#include \"stdint.h\"")
  self.emit("#include \"constants.h\"")
  self.emit("")
  self.emit("extern const uint8_t data[\{low.table_offset}];")
  self.emit("")

  for label, table in low.tables_by_label {
    self.emit("#define \{label} 0x\{hex(0x8000 + table.offset)}")
  }

  self.emit("")
  self.emit("#endif")
}
fn emit_data_c(self: Emitter, low: @lower.AsmLowering) -> Unit {
  self.emit("#include \"data.h\"")
  self.emit("")

  self.emit("const uint8_t data[] = {")
  self.indentation += 1

  for table in low.tables {
    self.emit("")
    self.emit("// \{table.name}")

    for line in table.lines {
      match line {
        Data(data~, comment~) => {
          let data = String::concat(data.map(show_expr), separator=", ")

          match comment.val {
            "" => self.emit(data + ",")
            _ => self.emit("\{data}, // \{comment.val}")
          }
        }
        Comment(comment) => {
          self.emit("// \{comment}")
        }
      }
    }
  }

  self.indentation -= 1
    self.emit("};\n")
}

fn emit_code_h(self: Emitter, low: @lower.AsmLowering) -> Unit {
  self.emit("#ifndef SMB_CODE_H")
  self.emit("#define SMB_CODE_H")
  self.emit("")
  self.emit("#include \"data.h\"")
  self.emit("#include \"instructions.h\"")
  self.emit("#include \"ppu.h\"")
  self.emit("#include \"apu.h\"")
  self.emit("")

  for sub in low.subroutines {
    self.emit("void \{rename_label(sub.name)}(void);")
  }

  self.emit("")

  self.emit("#endif")
}

fn rename_label(label: String) -> String {
  if label.ends_with("__sub") {
    label.split("__sub").iter().peek().unwrap()
  } else {
    label
  }
}

fn gen_jump(target: String, low: @lower.AsmLowering) -> String {
  let renamed_label = rename_label(target)

  match target {
    "__ret__" => "return;"
    _ => {
      if low.targets.subroutines.contains(target) {
        "\{renamed_label}(); return;"
      } else {
        "goto \{renamed_label};"
      }
    }
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

enum Reg { A; X; Y }
struct Range { min: Int; max: Int }

fn op_add(self: Range, other: Range) -> Range {
  { min: self.min + other.min, max: self.max + other.max }
}

fn op_sub(self: Range, other: Range) -> Range {
  { min: self.min - other.max, max: self.max - other.min }
}

fn compute_expr_range(expr: @lower.TypedExpr, low: @lower.AsmLowering) -> Range? {
  match expr {
    Const(value~, ..) => Some({ min: value, max: value })
    Ident(name~, ..) => match low.defines[name] {
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
    match addr {
      Ident(name="PPU_CTRL_REG1", ..) => "ppu_ctrl = \{target};"
      Ident(name="PPU_CTRL_REG2", ..) => "ppu_mask = \{target};"
      Ident(name="PPU_SPR_ADDR", ..) => "oam_addr = \{target};"
      Ident(name="PPU_SPR_DATA", ..) => "oam[oam_addr++] = \{target};"
      Ident(name="PPU_SCROLL_REG", ..) => "ppu_write_scroll(\{target});"
      Ident(name="PPU_ADDRESS", ..) => "ppu_write_address(\{target});"
      Ident(name="PPU_DATA", ..) => "ppu_write_data(\{target});"
      Ident(name="SPR_DMA", ..) => "ppu_transfer_oam((uint16_t)(\{target} << 8));"
      Ident(name="JOYPAD_PORT", ..) => "write_joypad1(\{target});"
      Ident(name="JOYPAD_PORT2", ..) => "write_joypad2(\{target});"
      Ident(
        name=("SND_REGISTER" | "SND_SQUARE1_REG" | "SND_SQUARE2_REG" | "SND_TRIANGLE_REG" |
        "SND_NOISE_REG" | "SND_DELTA_REG" | "SND_MASTERCTRL_REG") as name,
        ..
      ) => {
        "apu_write(\{name}, \{target});"
      }
      _ => {
        let value = "\{show_expr(addr)}\{suffix}"

        match compute_range(addr, mode, low) {
          Some({ min: _, max }) => {
            if max < 0x2000 {
              return "ram[\{value}] = \{target};"
            } else if max < 0x4000 {
              return "ppu_write_register(0x2000 + (\{show_expr(addr)} & 0b111), \{target});"
            }
          }
          None => ()
        }

        "dynamic_ram_write(\{value}, \{target});"
      }
    }
  }

  match mode {
      ZeroPage(addr) | Absolute(addr) => rewrite(addr, "")
      ZeroPageX(addr) | AbsoluteX(addr) => rewrite(addr, " + x")
      ZeroPageY(addr) | AbsoluteY(addr) => rewrite(addr, " + y")
      IndirectIndexed(addr) => "dynamic_ram_write(read_word(\{show_expr(addr)}) + y, \{target});"
      Implied | Immediate(_) | Relative(_) | Accumulator| Indirect(_) | IndexedIndirect(_) => abort("unreachable")
  }
}

fn emit_item(self: Emitter, item: @lower.AsmItem, low: @lower.AsmLowering) -> Unit {
    match item {
      Label(name) => {
        // do not output labels for subroutines
        let is_subroutine_label = low.is_subroutine_label(name);

        if not(is_subroutine_label) {
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
              _ => "\{op.to_string().to_lower()}\{mode.suffix()}(\{args});"
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

fn emit_basic_block(self: Emitter, bb: @lower.BasicBlock, low: @lower.AsmLowering) -> Unit {
  for item in bb.items {
    self.emit_item(item, low)
  }
}

fn emit_subroutine(self: Emitter, subroutine: @lower.AsmSubroutine, low: @lower.AsmLowering) -> Unit {
  self.indentation = 0
  self.emit("void \{rename_label(subroutine.name)}(void) {")
  self.indentation = 1

  for block in subroutine.blocks {
    self.emit_basic_block(block, low)
  }

  self.indentation = 0
  self.emit("}\n")
}

fn emit_flat_subroutine(self: Emitter, subroutine: @lower.AsmFlatSubroutine, low: @lower.AsmLowering) -> Unit {
  self.indentation = 0
  self.emit("void \{rename_label(subroutine.name)}(void) {")
  self.indentation = 1

  for item in subroutine.items {
    self.emit_item(item, low)
  }

  self.indentation = 0
  self.emit("}\n")
}

fn emit_code_c(self: Emitter, low: @lower.AsmLowering) -> Unit {
self.emit("#include \"code.h\"")
  self.emit("")

  for sub in low.flat_subroutines {
    self.emit_flat_subroutine(sub, low)
  }
}

pub(all) struct File {
  name: String
  contents: String
  is_lib: Bool
}

fn File::read(name~: String, path~: String, is_lib~: Bool = true) -> File!Error {
  let contents = @fs.read_file_to_string!(path~)
  { name, contents, is_lib }
}

pub fn write(self: File, dir~: String) -> Unit {
  @fs.write_string_to_file(path="\{dir}/\{self.name}", content=self.contents)
}

pub(all) struct Transpiler {
  files: Array[File]
}

pub fn Transpiler::new(low: @lower.AsmLowering) -> Transpiler!Error {
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

  let cpu_h = File::read!(name="cpu.h", path="src/c/cpu.h")
  let cpu_c = File::read!(name="cpu.c", path="src/c/cpu.c")
  let instructions_h = File::read!(name="instructions.h", path="src/c/instructions.h")
  let instructions_c = File::read!(name="instructions.c", path="src/c/instructions.c")
  let ppu_h = File::read!(name="ppu.h", path="src/c/ppu.h")
  let ppu_c = File::read!(name="ppu.c", path="src/c/ppu.c")
  let apu_h = File::read!(name="apu.h", path="src/c/apu.h")
  let apu_c = File::read!(name="apu.c", path="src/c/apu.c")
  let external_h = File::read!(name="external.h", path="src/c/external.h")
  let rec_h = File::read!(name="rec.h", path="src/c/rec.h")
  let rec_c = File::read!(name="rec.c", path="src/c/rec.c")
  let common_h = File::read!(name="common.h", path="src/c/common.h")
  let common_c = File::read!(name="common.c", path="src/c/common.c")
  let state_h = File::read!(name="state.h", path="src/c/state.h")
  let state_c = File::read!(name="state.c", path="src/c/state.c")
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
  
  let hash_c = File::read!(name="hash.c", path="src/c/hash.c", is_lib=false)
  let main_c = File::read!(name="main.c", path="src/c/main.c", is_lib=false)

  {
    files: [
      constants_h,
      data_h, data_c,
      cpu_h, cpu_c,
      instructions_h, instructions_c,
      code_h, code_c,
      ppu_h, ppu_c,
      apu_h, apu_c,
      external_h,
      rec_h, rec_c,
      common_h, common_c,
      main_c, hash_c,
      state_h, state_c,
    ],
  }
}
