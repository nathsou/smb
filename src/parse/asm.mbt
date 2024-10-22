
enum AsmValue {
  Const(~base: ConstantBase, ~value: Int, ~size: ConstantSize)
  Variable(~name: String)
  Label(String)
} derive (Show)

enum UnaryOp {
  GetLowByte; GetHighByte
} derive (Show)

enum BinaryOp {
  Add; Sub
} derive (Show)

enum AsmExpr {
  Const(~base: ConstantBase, ~value: Int, ~size: ConstantSize)
  Ident(String)
  Unary(~op: UnaryOp, ~expr: AsmExpr)
  Binary(~op: BinaryOp, ~lhs: AsmExpr, ~rhs: AsmExpr)
} derive (Show)

enum AddressingMode {
  Accumulator
  Implied
  Immediate(AsmExpr)
  Relative(~addr: AsmExpr)
  AbsoluteOrZeroPage(~addr: AsmExpr)
  AbsoluteOrZeroPageX(~addr: AsmExpr)
  AbsoluteOrZeroPageY(~addr: AsmExpr)
  Indirect(~addr: AsmExpr)
  IndirectX(~addr: AsmExpr)
  IndirectY(~addr: AsmExpr)
} derive (Show)

enum AsmInst {
  Label(String)
  Inst(~op: OpCode, ~mode: AddressingMode)
  Define(~name: String, ~rhs: AsmExpr)
  Directive(~name: String, ~args: Array[AsmExpr])
  Comment(String)
} derive (Show)

struct CommentedAsmInst {
  asm: AsmInst
  comment: String?
}
