
// pub enum AddressingMode {
//   Accumulator
//   Implied
//   Immediate(Int)
//   Relative(Int)
//   ZeroPage(Int)
//   ZeroPageX(Int)
//   ZeroPageY(Int)
//   Absolute(Int)
//   AbsoluteX(Int)
//   AbsoluteY(Int)
//   IndexedIndirect(Int)
//   IndirectIndexed(Int)
// } derive (Show)

pub enum Inst {
  Inst(~op: @parse.OpCode, ~mode: @lower.AsmAddressingMode)
  JumpEngine(~jump_table: Array[Int])
} derive (Show)
