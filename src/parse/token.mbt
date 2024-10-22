enum OpCode {
  ADC; AND; ASL; BCC; BCS; BEQ; BIT; BMI; BNE; BPL; BRK; BVC; BVS; CLC
  CLD; CLI; CLV; CMP; CPX; CPY; DEC; DEX; DEY; EOR; INC; INX; INY; JMP
  JSR; LDA; LDX; LDY; LSR; NOP; ORA; PHA; PHP; PLA; PLP; ROL; ROR; RTI
  RTS; SBC; SEC; SED; SEI; STA; STX; STY; TAX; TAY; TSX; TXA; TXS; TYA
} derive (Show, Eq)

enum ConstantBase {
  Binary; Decimal; Hexadecimal
} derive (Show, Eq)

enum Token {
  Comment(~comment: String, ~is_inline: Bool)
  Identifier(String)
  Label(String)
  Constant(~base: ConstantBase, ~value: Int)
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
