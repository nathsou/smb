
pub enum Inst {
  Inst(~op: @parse.OpCode, ~mode: @lower.AsmAddressingMode)
  JumpEngine(~jump_table: Array[Int])
} derive (Show)
