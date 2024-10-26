
pub enum Type {
  ByteTy; WordTy; TableTy; LabelTy
} derive (Show, Eq)

pub enum TypedExpr {
  Const(~base: @parse.ConstantBase, ~value: Int, ~size: @parse.ConstantSize, ~ty: Type)
  Ident(~name: String, ~ty: Type)
  Unary(~op: @parse.UnaryOp, ~expr: TypedExpr, ~ty: Type)
  Binary(~op: @parse.BinaryOp, ~lhs: TypedExpr, ~rhs: TypedExpr, ~ty: Type)
} derive (Show)

pub enum AsmAddressingMode {
  Accumulator
  Implied
  Immediate(TypedExpr)
  Relative(TypedExpr)
  ZeroPage(TypedExpr)
  ZeroPageX(TypedExpr)
  ZeroPageY(TypedExpr)
  Absolute(TypedExpr)
  AbsoluteX(TypedExpr)
  AbsoluteY(TypedExpr)
  Indirect(TypedExpr)
  IndexedIndirect(TypedExpr)
  IndirectIndexed(TypedExpr)
} derive (Show)

pub enum AsmItem {
  Label(String)
  Inst(~op: @parse.OpCode, ~mode: AsmAddressingMode)
  JumpEngine(~jump_table: Array[String])
  Comment(~comment: String, ~is_inline: Bool)
} derive (Show)

pub fn ty(self: TypedExpr) -> Type {
  match self {
    Const(base=_, value=_, size=_, ~ty) => ty
    Ident(name=_, ~ty) => ty
    Unary(op=_, expr=_, ~ty) => ty
    Binary(op=_, lhs=_, rhs=_, ~ty) => ty
  }
}
