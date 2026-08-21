// Address-aware assembly layout for the restricted x816 syntax used by SMB.
// It deliberately keeps every byte, including padding and bytes that are also
// decoded as overlapping instructions later in the pipeline.

pub(all) struct Image {
  origin: Int
  bytes: Array[Int]
  symbols: Map[String, Int]
  define_values: Map[String, Int]
} derive(Debug)

pub suberror LayoutError {
  UndefinedSymbol(String)
  UnsupportedDirective(String)
  UnsupportedInstruction(String)
  InvalidByte(Int)
  InvalidBranch(Int)
} derive(Debug)

priv struct Value {
  value: Int
  size: @parse.ConstantSize
}

fn mask(size: @parse.ConstantSize) -> Int {
  match size {
    Byte => 0xff
    Word => 0xffff
  }
}

fn max_size(lhs: @parse.ConstantSize, rhs: @parse.ConstantSize) -> @parse.ConstantSize {
  match (lhs, rhs) {
    (Word, _) | (_, Word) => Word
    _ => Byte
  }
}

priv struct Assembler {
  items: Array[@parse.SourceItem]
  symbols: Map[String, Int]
  defines: Map[String, @parse.SourceExpr]
  define_values: Map[String, Value]
  mut origin: Int
  mut pc: Int
  bytes: Array[Int]
}

pub fn layout(items: Array[@parse.SourceItem]) -> Image raise LayoutError {
  let assembler = Assembler::new(items)
  assembler.collect_defines()
  assembler.first_pass()
  assembler.second_pass()
  {
    origin: assembler.origin,
    bytes: assembler.bytes,
    symbols: assembler.symbols,
    define_values: assembler.define_values.map(fn(_name, value) { value.value }),
  }
}

fn Assembler::new(items: Array[@parse.SourceItem]) -> Assembler {
  {
    items,
    symbols: {},
    defines: {},
    define_values: {},
    origin: 0,
    pc: 0,
    bytes: [],
  }
}

fn Assembler::collect_defines(self: Assembler) -> Unit {
  for item in self.items {
    match item {
      Define(name~, rhs~) => self.defines[name] = rhs
      _ => ()
    }
  }
}

fn Assembler::eval_expr(self: Assembler, expr: @parse.SourceExpr) -> Value raise LayoutError {
  match expr {
    Const(value~, size~, ..) => { value, size }
    Ident(name) => {
      match self.define_values.get(name) {
        Some(value) => value
        None => match self.defines.get(name) {
          Some(expr) => {
            let value = self.eval_expr(expr)
            self.define_values[name] = value
            value
          }
          None => match self.symbols.get(name) {
            Some(value) => { value, size: Word }
            None => raise UndefinedSymbol(name)
          }
        }
      }
    }
    Unary(op~, expr~) => {
      let value = self.eval_expr(expr).value
      match op {
        GetLowByte => { value: value & 0xff, size: Byte }
        GetHighByte => { value: (value >> 8) & 0xff, size: Byte }
      }
    }
    Binary(op~, lhs~, rhs~) => {
      let lhs = self.eval_expr(lhs)
      let rhs = self.eval_expr(rhs)
      let size = max_size(lhs.size, rhs.size)
      let value = match op {
        Add => lhs.value + rhs.value
        Sub => lhs.value - rhs.value
      }
      { value: value & mask(size), size }
    }
  }
}

fn Assembler::is_zero_page(self: Assembler, expr: @parse.SourceExpr) -> Bool raise LayoutError {
  match self.eval_expr(expr) {
    { value, size: Byte } => value <= 0xff
    _ => false
  }
}

fn mode_size(self: Assembler, mode: @parse.SourceAddressingMode) -> Int raise LayoutError {
  match mode {
    Implied | Accumulator => 1
    Immediate(_) | Relative(_) | IndexedIndirect(_) | IndirectIndexed(_) => 2
    Indirect(_) => 3
    AbsoluteOrZeroPage(addr=expr) | AbsoluteOrZeroPageX(addr=expr) | AbsoluteOrZeroPageY(addr=expr) => {
      if self.is_zero_page(expr) { 2 } else { 3 }
    }
  }
}

fn directive_size(name: String, args: Array[@parse.SourceExpr]) -> Int raise LayoutError {
  match name {
    "db" => args.length()
    "dw" => args.length() * 2
    "org" | "index" | "mem" => 0
    _ => raise UnsupportedDirective(name)
  }
}

fn Assembler::apply_org(self: Assembler, args: Array[@parse.SourceExpr]) -> Unit raise LayoutError {
  if args.length() != 1 {
    raise UnsupportedDirective("org")
  }
  let value = self.eval_expr(args[0]).value
  if self.bytes.is_empty() {
    self.origin = value
  }
  self.pc = value
}

fn Assembler::first_pass(self: Assembler) -> Unit raise LayoutError {
  self.pc = 0
  for item in self.items {
    match item {
      Label(name) => self.symbols[name] = self.pc
      Inst(mode~, ..) => self.pc += self.mode_size(mode)
      Directive(name="org", args~) => self.apply_org(args)
      Directive(name~, args~) => self.pc += directive_size(name, args)
      _ => ()
    }
  }
}

fn Assembler::emit_byte(self: Assembler, value: Int) -> Unit raise LayoutError {
  if value < 0 || value > 0xff {
    raise InvalidByte(value)
  }
  self.bytes.push(value)
  self.pc += 1
}

fn Assembler::emit_word(self: Assembler, value: Int) -> Unit raise LayoutError {
  self.emit_byte(value & 0xff)
  self.emit_byte((value >> 8) & 0xff)
}

fn opcode(op: @parse.OpCode, mode: String) -> Int? {
  match (op, mode) {
    (BRK, "imp") => Some(0x00)
    (ORA, "indx") => Some(0x01)
    (ORA, "zp") => Some(0x05)
    (ASL, "acc") => Some(0x0a)
    (ORA, "imm") => Some(0x09)
    (ORA, "abs") => Some(0x0d)
    (ASL, "abs") => Some(0x0e)
    (BPL, "rel") => Some(0x10)
    (ORA, "indy") => Some(0x11)
    (ORA, "zpx") => Some(0x15)
    (ASL, "zpx") => Some(0x16)
    (CLC, "imp") => Some(0x18)
    (ORA, "absy") => Some(0x19)
    (ORA, "absx") => Some(0x1d)
    (ASL, "absx") => Some(0x1e)
    (JSR, "abs") => Some(0x20)
    (AND, "indx") => Some(0x21)
    (BIT, "zp") => Some(0x24)
    (AND, "zp") => Some(0x25)
    (ROL, "zp") => Some(0x26)
    (PLP, "imp") => Some(0x28)
    (AND, "imm") => Some(0x29)
    (ROL, "acc") => Some(0x2a)
    (BIT, "abs") => Some(0x2c)
    (AND, "abs") => Some(0x2d)
    (ROL, "abs") => Some(0x2e)
    (BMI, "rel") => Some(0x30)
    (AND, "indy") => Some(0x31)
    (AND, "zpx") => Some(0x35)
    (ROL, "zpx") => Some(0x36)
    (SEC, "imp") => Some(0x38)
    (AND, "absy") => Some(0x39)
    (AND, "absx") => Some(0x3d)
    (ROL, "absx") => Some(0x3e)
    (RTI, "imp") => Some(0x40)
    (EOR, "indx") => Some(0x41)
    (EOR, "zp") => Some(0x45)
    (LSR, "zp") => Some(0x46)
    (PHA, "imp") => Some(0x48)
    (EOR, "imm") => Some(0x49)
    (LSR, "acc") => Some(0x4a)
    (JMP, "abs") => Some(0x4c)
    (EOR, "abs") => Some(0x4d)
    (LSR, "abs") => Some(0x4e)
    (BVC, "rel") => Some(0x50)
    (EOR, "indy") => Some(0x51)
    (EOR, "zpx") => Some(0x55)
    (LSR, "zpx") => Some(0x56)
    (CLI, "imp") => Some(0x58)
    (EOR, "absy") => Some(0x59)
    (EOR, "absx") => Some(0x5d)
    (LSR, "absx") => Some(0x5e)
    (RTS, "imp") => Some(0x60)
    (ADC, "indx") => Some(0x61)
    (ADC, "zp") => Some(0x65)
    (ROR, "zp") => Some(0x66)
    (PLA, "imp") => Some(0x68)
    (ADC, "imm") => Some(0x69)
    (ROR, "acc") => Some(0x6a)
    (JMP, "ind") => Some(0x6c)
    (ADC, "abs") => Some(0x6d)
    (ROR, "abs") => Some(0x6e)
    (BVS, "rel") => Some(0x70)
    (ADC, "indy") => Some(0x71)
    (ADC, "zpx") => Some(0x75)
    (ROR, "zpx") => Some(0x76)
    (SEI, "imp") => Some(0x78)
    (ADC, "absy") => Some(0x79)
    (ADC, "absx") => Some(0x7d)
    (ROR, "absx") => Some(0x7e)
    (NOP, "imp") => Some(0xea)
    (STA, "indx") => Some(0x81)
    (STY, "zp") => Some(0x84)
    (STA, "zp") => Some(0x85)
    (STX, "zp") => Some(0x86)
    (DEY, "imp") => Some(0x88)
    (TXA, "imp") => Some(0x8a)
    (STY, "abs") => Some(0x8c)
    (STA, "abs") => Some(0x8d)
    (STX, "abs") => Some(0x8e)
    (BCC, "rel") => Some(0x90)
    (STA, "indy") => Some(0x91)
    (STY, "zpx") => Some(0x94)
    (STA, "zpx") => Some(0x95)
    (STX, "zpy") => Some(0x96)
    (TYA, "imp") => Some(0x98)
    (STA, "absy") => Some(0x99)
    (TXS, "imp") => Some(0x9a)
    (STA, "absx") => Some(0x9d)
    (LDY, "imm") => Some(0xa0)
    (LDA, "indx") => Some(0xa1)
    (LDX, "imm") => Some(0xa2)
    (LDY, "zp") => Some(0xa4)
    (LDA, "zp") => Some(0xa5)
    (LDX, "zp") => Some(0xa6)
    (TAY, "imp") => Some(0xa8)
    (LDA, "imm") => Some(0xa9)
    (TAX, "imp") => Some(0xaa)
    (LDY, "abs") => Some(0xac)
    (LDA, "abs") => Some(0xad)
    (LDX, "abs") => Some(0xae)
    (BCS, "rel") => Some(0xb0)
    (LDA, "indy") => Some(0xb1)
    (LDY, "zpx") => Some(0xb4)
    (LDA, "zpx") => Some(0xb5)
    (LDX, "zpy") => Some(0xb6)
    (CLV, "imp") => Some(0xb8)
    (LDA, "absy") => Some(0xb9)
    (TSX, "imp") => Some(0xba)
    (LDY, "absx") => Some(0xbc)
    (LDA, "absx") => Some(0xbd)
    (LDX, "absy") => Some(0xbe)
    (CPY, "imm") => Some(0xc0)
    (CMP, "indx") => Some(0xc1)
    (CPY, "zp") => Some(0xc4)
    (CMP, "zp") => Some(0xc5)
    (DEC, "zp") => Some(0xc6)
    (INY, "imp") => Some(0xc8)
    (CMP, "imm") => Some(0xc9)
    (DEX, "imp") => Some(0xca)
    (CPY, "abs") => Some(0xcc)
    (CMP, "abs") => Some(0xcd)
    (DEC, "abs") => Some(0xce)
    (BNE, "rel") => Some(0xd0)
    (CMP, "indy") => Some(0xd1)
    (CMP, "zpx") => Some(0xd5)
    (DEC, "zpx") => Some(0xd6)
    (CLD, "imp") => Some(0xd8)
    (CMP, "absy") => Some(0xd9)
    (CMP, "absx") => Some(0xdd)
    (DEC, "absx") => Some(0xde)
    (CPX, "imm") => Some(0xe0)
    (SBC, "indx") => Some(0xe1)
    (CPX, "zp") => Some(0xe4)
    (SBC, "zp") => Some(0xe5)
    (INC, "zp") => Some(0xe6)
    (INX, "imp") => Some(0xe8)
    (SBC, "imm") => Some(0xe9)
    (NOP, "imp") => Some(0xea)
    (CPX, "abs") => Some(0xec)
    (SBC, "abs") => Some(0xed)
    (INC, "abs") => Some(0xee)
    (BEQ, "rel") => Some(0xf0)
    (SBC, "indy") => Some(0xf1)
    (SBC, "zpx") => Some(0xf5)
    (INC, "zpx") => Some(0xf6)
    (SED, "imp") => Some(0xf8)
    (SBC, "absy") => Some(0xf9)
    (SBC, "absx") => Some(0xfd)
    (INC, "absx") => Some(0xfe)
    _ => None
  }
}

fn Assembler::emit_instruction(self: Assembler, op: @parse.OpCode, mode: @parse.SourceAddressingMode) -> Unit raise LayoutError {
  let (kind, operand) = match mode {
    Implied => ("imp", None)
    Accumulator => ("acc", None)
    Immediate(expr) => ("imm", Some(expr))
    Relative(addr~) => ("rel", Some(addr))
    Indirect(addr=expr) => ("ind", Some(expr))
    IndexedIndirect(addr=expr) => ("indx", Some(expr))
    IndirectIndexed(addr=expr) => ("indy", Some(expr))
    AbsoluteOrZeroPage(addr=expr) => (if self.is_zero_page(expr) { "zp" } else { "abs" }, Some(expr))
    AbsoluteOrZeroPageX(addr=expr) => (if self.is_zero_page(expr) { "zpx" } else { "absx" }, Some(expr))
    AbsoluteOrZeroPageY(addr=expr) => (if self.is_zero_page(expr) { "zpy" } else { "absy" }, Some(expr))
  }
  let byte = opcode(op, kind).unwrap_or_error(UnsupportedInstruction("\{@debug.to_string(op)} / \{kind}"))
  let instruction_pc = self.pc
  self.emit_byte(byte)
  match operand {
    None => ()
    Some(expr) => {
      let value = self.eval_expr(expr).value
      match kind {
        "rel" => {
          let offset = value - (instruction_pc + 2)
          if offset < -128 || offset > 127 {
            raise InvalidBranch(offset)
          }
          self.emit_byte(offset & 0xff)
        }
        "imm" | "zp" | "zpx" | "zpy" | "indx" | "indy" => self.emit_byte(value & 0xff)
        _ => self.emit_word(value)
      }
    }
  }
}

fn Assembler::second_pass(self: Assembler) -> Unit raise LayoutError {
  self.pc = self.origin
  for item in self.items {
    match item {
      Inst(op~, mode~) => self.emit_instruction(op, mode)
      Directive(name="org", args~) => self.apply_org(args)
      Directive(name="index" | "mem", ..) => ()
      Directive(name="db", args~) => {
        for arg in args {
          self.emit_byte(self.eval_expr(arg).value & 0xff)
        }
      }
      Directive(name="dw", args~) => {
        for arg in args {
          self.emit_word(self.eval_expr(arg).value)
        }
      }
      Directive(name~, ..) => raise UnsupportedDirective(name)
      _ => ()
    }
  }
}

pub fn Image::read_byte(self: Image, address: Int) -> Int? {
  let index = address - self.origin
  if index < 0 || index >= self.bytes.length() {
    None
  } else {
    Some(self.bytes[index])
  }
}

pub fn Image::read_word(self: Image, address: Int) -> Int? {
  match (self.read_byte(address), self.read_byte(address + 1)) {
    (Some(lo), Some(hi)) => Some(lo | (hi << 8))
    _ => None
  }
}
