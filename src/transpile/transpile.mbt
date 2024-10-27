
pub struct Emitter {
  output: StringBuilder
  mut indentation: Int
}

pub fn Emitter::new() -> Emitter {
  {
    output: StringBuilder::new(),
    indentation: 0,
  }
}

fn emit(self: Emitter, s: String, ~newline: Bool = true) -> Unit {
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

fn show_const(~base: @parse.ConstantBase, ~value: Int) -> String {
  match base {
    Decimal => value.to_string()
    Hexadecimal => "0x\{hex(value)}"
    Binary => "0b\{bin(value)}"
  }
}

fn show_expr(expr: @lower.TypedExpr) -> String {
  match expr {
    Const(~base, ~value, size=_, ty=_) => show_const(~base, ~value)
    Ident(~name, ty=_) => name
    Unary(~op, ~expr, ty=_) => {
      let macro = match op {
        GetHighByte => "HIGH_BYTE"
        GetLowByte => "LOW_BYTE"
      }

      "\{macro}(\{show_expr(expr)})"
    }
    Binary(~op, ~lhs, ~rhs, ty=_) => {
      let op = match op {
        Add => "+"
        Sub => "-"
      }

      "\{show_expr(lhs)} \{op} \{show_expr(rhs)}"
    }
  }
}

pub type! EmitterError String derive(Show)

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
        Data(~data, ~comment) => {
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

fn emit_code_h(self: Emitter) -> Unit {
  self.emit("#ifndef SMB_CODE_H")
  self.emit("#define SMB_CODE_H")
  self.emit("")
  self.emit("#include \"data.h\"")
  self.emit("#include \"instructions.h\"")
  self.emit("")
  self.emit("typedef enum { RUN_STATE_RESET, RUN_STATE_NMI_HANDLER } RunState;")
  self.emit("")
  self.emit("void smb(RunState state);")
  self.emit("")
  self.emit("#endif")
}

fn emit_code_c(self: Emitter, low: @lower.AsmLowering) -> Unit {
  self.emit("#include \"code.h\"")
  self.emit("")
  self.emit("void smb(RunState state) {")
  self.indentation += 1

  self.emit("switch (state) {")
  self.indentation += 1
  self.emit("case RUN_STATE_RESET: goto Start;")
  self.emit("case RUN_STATE_NMI_HANDLER: goto NonMaskableInterrupt;")
  self.indentation -= 1
  self.emit("}\n")

  let mut jsr_return_index = 0
  let mut is_inside_jump_engine = false

  for item in low.lowered {
    match item {
      Label(name) => {
        is_inside_jump_engine = name == "JumpEngine"
        self.indentation = 1
        self.emit("\n\{name}:")
        self.indentation = 2
      }
      Inst(~op, ~mode, ~comment) => {
        if is_inside_jump_engine {
          continue
        }

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

        let stmt = match (op, is_relative) {
          (BCC | BCS | BEQ | BMI | BNE | BPL | BVC | BVS , true) => {
            let cond = match op {
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

            let target = match mode {
              Relative(expr) => "\{show_expr(expr)}"
              _ => abort("unreachable")
            }

            "if (\{cond}) { goto \{target}; }"
          }
          (JMP, _) => {
            match args {
              "EndlessLoop" => "return; // jmp EndlessLoop"
              _ => "goto \{args};"
            }
          }
          (JSR, _) => {
            jsr_return_index += 1

            let target = match mode {
              Absolute(Ident(~name, ty=_)) => name
              _ => ""
            }
            
            "jsr(\{target}, \{jsr_return_index - 1});"
          }
          (RTS, _) => "goto rts;"
          (RTI, _) => "return; // rti"
          _ => "\{op.to_string().to_lower()}\{mode.suffix()}(\{args});"
        }
        
        let stmt = match comment {
          Some(comment) => {
            "\{stmt} // \{comment}"
          }
          None => stmt
        }

        self.emit(stmt)
      }
      Comment(comment) => {
        self.emit("// \{comment}")
      }
      JumpEngine(~jump_table) => {
        self.emit("// jsr JumpEngine")
        self.emit("switch (a) {")
        self.indentation += 1

        for idx, label in jump_table {
          self.emit("case \{idx}: goto \{label};")
        }

        self.indentation -= 1
        self.emit("}")
      }
    }
  }

  self.emit("")
  self.indentation = 0
  self.emit("rts:")
  self.indentation += 1
  self.emit("switch (pop_jsr()) {")
  self.indentation += 1

  for i in 0..<jsr_return_index {
    self.emit("case \{i}: goto jsr_ret_\{i};")
  }

  self.indentation -= 1
  self.emit("}")
  self.indentation = 0
  self.emit("}")
}

pub struct File {
  name: String
  contents: String
}

fn File::read(~name: String, ~path: String) -> File!Error {
  let contents = @fs.read_file_to_string!(~path)
  { name, contents: contents }
}

pub fn write(self: File, ~dir: String) -> Unit {
  @fs.write_string_to_file(path="\{dir}/\{self.name}", content=self.contents)
}

pub struct Transpiler {
  files: Array[File]
}

pub fn Transpiler::new(low: @lower.AsmLowering) -> Transpiler!Error {
  let constants_h = {
    name: "constants.h",
    contents: {
      let emitter = Emitter::new()
      emitter.emit_constants_h(low)
      emitter.output.to_string()
    }
  }

  let data_h = {
    name: "data.h",
    contents: {
      let emitter = Emitter::new()
      emitter.emit_data_h(low)
      emitter.output.to_string()
    }
  }

  let data_c = {
    name: "data.c",
    contents: {
      let emitter = Emitter::new()
      emitter.emit_data_c(low)
      emitter.output.to_string()
    }
  }

  let cpu_h = File::read!(name="cpu.h", path="src/c/cpu.h")
  let cpu_c = File::read!(name="cpu.c", path="src/c/cpu.c")
  let instructions_h = File::read!(name="instructions.h", path="src/c/instructions.h")
  let instructions_c = File::read!(name="instructions.c", path="src/c/instructions.c")
  let ppu_h = File::read!(name="ppu.h", path="src/c/ppu.h")
  let ppu_c = File::read!(name="ppu.c", path="src/c/ppu.c")

  let code_h = {
    name: "code.h",
    contents: {
      let emitter = Emitter::new()
      emitter.emit_code_h()
      emitter.output.to_string()
    }
  }

  let code_c = {
    name: "code.c",
    contents: {
      let emitter = Emitter::new()
      emitter.emit_code_c(low)
      emitter.output.to_string()
    }
  }

  let main_c = File::read!(name="main.c", path="src/c/main.c")

  {
    files: [
      constants_h,
      data_h, data_c,
      cpu_h, cpu_c,
      instructions_h, instructions_c,
      code_h, code_c,
      ppu_h, ppu_c,
      main_c,
    ],
  }
}
