
pub enum AsmValue {
  Const(
    ~base: @parse.ConstantBase,
    ~value: Int,
    ~size: @parse.ConstantSize,
  )
  DataTableOffset(~offset: Int, ~label: String?)
  JumpTarget(String)
} derive (Show)

pub enum AsmAddressingMode {
  Accumulator
  Implied
  Immediate(AsmValue)
  Relative(~addr: AsmValue)
  ZeroPage(~addr: AsmValue)
  ZeroPageX(~addr: AsmValue)
  ZeroPageY(~addr: AsmValue)
  Absolute(~addr: AsmValue)
  AbsoluteX(~addr: AsmValue)
  AbsoluteY(~addr: AsmValue)
  Indirect(~addr: AsmValue)
  IndexedIndirect(~addr: AsmValue)
  IndirectIndexed(~addr: AsmValue)
} derive (Show)

pub enum AsmItem {
  Label(String)
  Inst(~op: @parse.OpCode, ~mode: AsmAddressingMode)
  JumpEngine(~jump_table: Array[String])
  Comment(~comment: String, ~is_inline: Bool)
} derive (Show)
