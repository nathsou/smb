
pub struct Emitter {
  output: StringBuilder
  indentation: Int
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

fn hex(n: Int) -> String {
  let mut n = n
  let h = StringBuilder::new()

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

pub fn emit_defines(self: Emitter, low: @lower.AsmLowering) -> Unit {
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

pub struct File {
  name: String
  contents: String
}

pub fn write(self: File, ~dir: String) -> Unit {
  @fs.write_string_to_file(path="\{dir}/\{self.name}", content=self.contents)
}

pub struct Transpiler {
  constants: File
}

pub fn Transpiler::new(low: @lower.AsmLowering) -> Transpiler {

  {
    constants: {
      name: "constants.h",
      contents: {
        let emitter = Emitter::new()
        emitter.emit_defines(low)
        emitter.output.to_string()
      }
    }
  }
}
