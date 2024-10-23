pub enum OpCode {
  ADC; AND; ASL; BCC; BCS; BEQ; BIT; BMI; BNE; BPL; BRK; BVC; BVS; CLC
  CLD; CLI; CLV; CMP; CPX; CPY; DEC; DEX; DEY; EOR; INC; INX; INY; JMP
  JSR; LDA; LDX; LDY; LSR; NOP; ORA; PHA; PHP; PLA; PLP; ROL; ROR; RTI
  RTS; SBC; SEC; SED; SEI; STA; STX; STY; TAX; TAY; TSX; TXA; TXS; TYA
} derive (Show, Eq)

pub enum ConstantBase {
  Binary; Decimal; Hexadecimal
} derive (Show, Eq)

pub enum ConstantSize {
  Byte; Word
} derive (Show, Eq)

enum Token {
  Comment(~comment: String, ~is_inline: Bool)
  Identifier(String)
  Label(String)
  Constant(~base: ConstantBase, ~value: Int, ~size: ConstantSize)
  OpCode(OpCode)
  Directive(String)
  Equal
  Comma
  Hash
  Plus
  Minus
  LeftParen
  RightParen
  LeftChevron
  RightChevron
} derive (Show, Eq)

fn to_int(self: ConstantBase) -> Int {
  match self {
    Binary => 2
    Decimal => 10
    Hexadecimal => 16
  }
}

pub fn is_implied(self: OpCode) -> Bool {
  match self {
    BRK | CLC | CLD | CLI | CLV | DEX | DEY | INX | INY | NOP | PHA | PHP
    | PLA | PLP | RTI | RTS | SEC | SED | SEI | TAX | TAY | TSX | TXA | TXS
    | TYA => true
    _ => false
  }
}

pub fn is_relative(self: OpCode) -> Bool {
  match self {
    BCC | BCS | BEQ | BMI | BNE | BPL | BVC | BVS => true
    _ => false
  }
}

pub fn supports_accumulator_addr_mode(self: OpCode) -> Bool {
  match self {
    ASL | LSR | ROL | ROR => true
    _ => false
  }
}

pub fn byte_size(self: ConstantSize) -> Int {
  match self {
    Byte => 1
    Word => 2
  }
}
