
enum AsmOperand {
  Const(~base: @parse.ConstantBase, ~value: Int, ~size: @parse.ConstantSize)
  Label(String)
  Variable(String)
} derive (Show)

enum AsmValue {
  Const(~base: @parse.ConstantBase, ~value: Int, ~size: @parse.ConstantSize)
  DataTableOffset(~offset: Int, ~label: String?)
  SubroutineLabel(String)
} derive (Show)

enum AsmAddressingMode {
  Accumulator
  Implied
  Immediate(AsmOperand)
  Relative(~addr: AsmOperand)
  ZeroPage(~addr: AsmOperand)
  ZeroPageX(~addr: AsmOperand)
  ZeroPageY(~addr: AsmOperand)
  Absolute(~addr: AsmOperand)
  AbsoluteX(~addr: AsmOperand)
  AbsoluteY(~addr: AsmOperand)
  Indirect(~addr: AsmOperand)
  IndirectX(~addr: AsmOperand)
  IndirectY(~addr: AsmOperand)
} derive (Show)

enum AsmItem {
  Label(String)
  Inst(~op: @parse.OpCode, ~mode: AsmAddressingMode)
  Define(~name: String, ~value: AsmValue)
  Comment(String)
} derive (Show)
