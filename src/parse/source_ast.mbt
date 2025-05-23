// source-level AST

pub(all) enum UnaryOp {
  GetLowByte; GetHighByte
} derive (Show)

pub(all) enum BinaryOp {
  Add; Sub
} derive (Show)

pub(all) enum SourceExpr {
  Const(base~: ConstantBase, value~: Int, size~: ConstantSize)
  Ident(String)
  Unary(op~: UnaryOp, expr~: SourceExpr)
  Binary(op~: BinaryOp, lhs~: SourceExpr, rhs~: SourceExpr)
} derive (Show)

// https://www.nesdev.org/wiki/CPU_addressing_modes
pub(all) enum SourceAddressingMode {
  Accumulator
  Implied
  Immediate(SourceExpr)
  Relative(addr~: SourceExpr)
  AbsoluteOrZeroPage(addr~: SourceExpr)
  AbsoluteOrZeroPageX(addr~: SourceExpr)
  AbsoluteOrZeroPageY(addr~: SourceExpr)
  Indirect(addr~: SourceExpr)
  IndexedIndirect(addr~: SourceExpr) // (d,x)
  IndirectIndexed(addr~: SourceExpr) // (d),y
} derive (Show)

pub(all) enum SourceItem {
  Label(String)
  Inst(op~: OpCode, mode~: SourceAddressingMode)
  Define(name~: String, rhs~: SourceExpr)
  Directive(name~: String, args~: Array[SourceExpr])
  Comment(comment~: String, is_inline~: Bool)
} derive (Show)
