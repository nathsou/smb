
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

fn emit(self: Emitter, s: String) -> Unit {
  let indent = "  ".repeat(self.indentation)
  self.output.write_string("\{indent}\{s}\n")
}

fn write(self: Emitter, s: String) -> Unit {
  self.output.write_string(s)
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

pub fn emit_constants_h(self: Emitter, low: @lower.AsmLowering) -> Unit {
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

pub fn emit_data_h(self: Emitter, low: @lower.AsmLowering) -> Unit {
  self.emit("#ifndef SMB_DATA_H")
  self.emit("#define SMB_DATA_H")
  self.emit("")
  self.emit("#include \"stdint.h\"")
  self.emit("#include \"constants.h\"")
  self.emit("")

  for label, table in low.tables_by_label {
    self.emit("#define \{label} 0x\{hex(0x8000 + table.offset)}")
  }

  self.emit("")
  self.emit("#endif")
}

pub fn emit_data_c(self: Emitter, low: @lower.AsmLowering) -> Unit {
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

pub struct File {
  name: String
  contents: String
}

pub fn write(self: File, ~dir: String) -> Unit {
  @fs.write_string_to_file(path="\{dir}/\{self.name}", content=self.contents)
}

pub struct Transpiler {
  files: Array[File]
}

pub fn Transpiler::new(low: @lower.AsmLowering) -> Transpiler {
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

  { files: [constants_h, data_h, data_c] }
}
