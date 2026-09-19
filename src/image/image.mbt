// Source addresses are semantic identity. Labels and comments are views over
// the bytes, not boundaries that force a code/data partition or a C function.
pub(all) struct Image {
  origin: Int
  bytes: Array[Int]
  symbols: Map[String, Int]
  addresses: Array[Int]
  source: Array[@parse.SourceItem]
} derive(Debug)

pub suberror ImageError { InvalidImage(String) } derive(Debug)

fn value(expr: @parse.SourceExpr, symbols: Map[String, Int], defines: Map[String, @parse.SourceExpr], active: Set[String]) -> Int raise ImageError {
  match expr {
    Const(value~, ..) => value
    Ident(name) => {
      if active.contains(name) { raise InvalidImage("Cyclic definition: \{name}") }
      match defines.get(name) {
        Some(expr) => {
          active.add(name)
          let result = value(expr, symbols, defines, active)
          active.remove(name)
          result
        }
        None => symbols.get(name).unwrap_or_error(InvalidImage("Undefined symbol: \{name}"))
      }
    }
    Unary(op~, expr~) => {
      let v = value(expr, symbols, defines, active)
      match op { GetLowByte => v & 255; GetHighByte => (v >> 8) & 255 }
    }
    Binary(op~, lhs~, rhs~) => {
      let a = value(lhs, symbols, defines, active)
      let b = value(rhs, symbols, defines, active)
      match op { Add => a + b; Sub => a - b }
    }
  }
}

pub fn Image::eval(self: Image, expr: @parse.SourceExpr) -> Int raise ImageError {
  let defines: Map[String, @parse.SourceExpr] = Map([])
  for item in self.source {
    if item is Define(name~, rhs~) { defines[name] = rhs }
  }
  value(expr, self.symbols, defines, Set([]))
}

pub fn operand(mode: @parse.SourceAddressingMode) -> @parse.SourceExpr? {
  match mode {
    Implied | Accumulator => None
    Immediate(e) | Relative(addr=e) | AbsoluteOrZeroPage(addr=e) |
    AbsoluteOrZeroPageX(addr=e) | AbsoluteOrZeroPageY(addr=e) |
    Indirect(addr=e) | IndexedIndirect(addr=e) | IndirectIndexed(addr=e) => Some(e)
  }
}

fn form(op: @parse.OpCode, mode: @parse.SourceAddressingMode, resolved: Int?) -> String {
  let zp = match resolved { Some(v) => v >= 0 && v < 256; None => false }
  match mode {
    Implied => "imp"
    Accumulator => "acc"
    Immediate(_) => "imm"
    Relative(_) => "rel"
    Indirect(_) => "ind"
    IndexedIndirect(_) => "indx"
    IndirectIndexed(_) => "indy"
    AbsoluteOrZeroPage(_) => if zp && encoding(op, "zp") != None { "zp" } else { "abs" }
    AbsoluteOrZeroPageX(_) => if zp && encoding(op, "zpx") != None { "zpx" } else { "absx" }
    AbsoluteOrZeroPageY(_) => if zp && encoding(op, "zpy") != None { "zpy" } else { "absy" }
  }
}

pub fn width(form: String) -> Int {
  match form {
    "imp" | "acc" => 1
    "abs" | "absx" | "absy" | "ind" => 3
    _ => 2
  }
}

pub fn assemble(source: Array[@parse.SourceItem]) -> Image raise ImageError {
  let symbols: Map[String, Int] = Map([])
  let defines: Map[String, @parse.SourceExpr] = Map([])
  for item in source {
    if item is Define(name~, rhs~) {
      if defines.contains(name) { raise InvalidImage("Duplicate definition: \{name}") }
      defines[name] = rhs
    }
  }
  let addresses = Array::make(source.length(), 0)
  let forms = Array::make(source.length(), "")
  let mut origin = 0x10000
  let mut end = 0
  let mut converged = false
  // Relax zero-page encodings until forward labels and instruction sizes agree.
  for _ in 0..<32 {
    let mut pc = 0
    let mut changed = false
    origin = 0x10000
    end = 0
    let seen: Set[String] = Set([])
    for i, item in source {
      addresses[i] = pc
      match item {
        Label(name) => {
          if seen.contains(name) || defines.contains(name) { raise InvalidImage("Duplicate label: \{name}") }
          seen.add(name)
          if symbols.get(name) != Some(pc) { symbols[name] = pc; changed = true }
        }
        Inst(op~, mode~) => {
          let resolved = match operand(mode) {
            Some(expr) => try Some(value(expr, symbols, defines, Set([]))) catch { _ => None }
            None => None
          }
          let kind = form(op, mode, resolved)
          if encoding(op, kind) == None { raise InvalidImage("Unsupported encoding: \{@debug.to_string(op)} \{kind}") }
          if forms[i] != kind { forms[i] = kind; changed = true }
          origin = math_min(origin, pc)
          pc += width(kind)
          end = math_max(end, pc)
        }
        Directive(name="org", args~) => {
          if args.length() != 1 { raise InvalidImage(".org requires one address") }
          pc = value(args[0], symbols, defines, Set([]))
          if pc < 0 || pc > 65535 { raise InvalidImage("Invalid origin") }
          addresses[i] = pc
        }
        Directive(name="db" | "dw" as name, args~) => {
          origin = math_min(origin, pc)
          pc += args.length() * (if name == "dw" { 2 } else { 1 })
          end = math_max(end, pc)
        }
        Directive(name="index" | "mem", ..) | Define(_) | Comment(_) => ()
        Directive(name~, ..) => raise InvalidImage("Unsupported directive: \{name}")
      }
    }
    if !changed { converged = true; break }
  }
  if !converged || origin > end || end > 65536 { raise InvalidImage("Assembly layout did not converge in the 16-bit address space") }
  let bytes = Array::make(end - origin, 0)
  let written: Set[Int] = Set([])
  fn put(address: Int, v: Int) -> Unit raise ImageError {
    if written.contains(address) { raise InvalidImage("Overlapping .org output at \{address}") }
    written.add(address)
    bytes[address - origin] = v & 255
  }
  for i, item in source {
    let pc = addresses[i]
    match item {
      Inst(op~, mode~) => {
        let kind = forms[i]
        put(pc, encoding(op, kind).unwrap())
        if operand(mode) is Some(expr) {
          let v = value(expr, symbols, defines, Set([]))
          if kind == "rel" {
            let delta = v - (pc + 2)
            if delta < -128 || delta > 127 { raise InvalidImage("Branch out of range at \{pc}: \{delta}") }
            put(pc + 1, delta)
          } else {
            if v < 0 || v >= (if width(kind) == 2 { 256 } else { 65536 }) { raise InvalidImage("Operand out of range at \{pc}: \{v}") }
            put(pc + 1, v)
            if width(kind) == 3 { put(pc + 2, v >> 8) }
          }
        }
      }
      Directive(name="db" | "dw" as name, args~) => {
        let size = if name == "dw" { 2 } else { 1 }
        for j, expr in args {
          let v = value(expr, symbols, defines, Set([]))
          if v < 0 || v >= (if size == 1 { 256 } else { 65536 }) { raise InvalidImage("Data out of range at \{pc}: \{v}") }
          put(pc + j * size, v)
          if size == 2 { put(pc + j * size + 1, v >> 8) }
        }
      }
      _ => ()
    }
  }
  { origin, bytes, symbols, addresses, source }
}

fn math_min(a: Int, b: Int) -> Int { if a < b { a } else { b } }
fn math_max(a: Int, b: Int) -> Int { if a > b { a } else { b } }

pub fn Image::read(self: Image, address: Int) -> Int raise ImageError {
  let index = if self.bytes.length() == 16384 && self.origin >= 0x8000 && address >= 0x8000 {
    (address - 0x8000) & 0x3fff
  } else { address - self.origin }
  self.bytes.get(index).unwrap_or_error(InvalidImage("Address outside image: \{address}"))
}

pub fn Image::word(self: Image, address: Int) -> Int raise ImageError {
  self.read(address) | (self.read(address + 1) << 8)
}
