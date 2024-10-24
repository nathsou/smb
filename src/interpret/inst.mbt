
pub enum AddressingMode {
  Accumulator
  Implied
  Immediate(~const: Int)
  Relative(~target: Int)
  ZeroPage(~addr: Int)
  ZeroPageX(~data_offset: Int)
  ZeroPageY(~data_offset: Int)
  Absolute(~addr: Int)
  AbsoluteX(~data_offset: Int)
  AbsoluteY(~data_offset: Int)
  IndexedIndirect(~data_offset: Int)
  IndirectIndexed(~data_offset: Int)
} derive (Show)

pub enum Inst {
  Inst(~op: @parse.OpCode, ~mode: AddressingMode)
  JumpEngine(~jump_table: Array[Int])
} derive (Show)
