
pub enum Type {
  ConstTy(@parse.ConstantSize)
  ArrayTy(@parse.ConstantSize)
  LabelTy
} derive (Show, Eq)

pub enum TypedExpr {
  Const(
    ~base: @parse.ConstantBase,
    ~value: Int,
    ~size: @parse.ConstantSize,
    ~ty: Type,
  )
  Ident(~name: String, ~ty: Type)
  Unary(~op: @parse.UnaryOp, ~expr: TypedExpr, ~ty: Type)
  Binary(~op: @parse.BinaryOp, ~lhs: TypedExpr, ~rhs: TypedExpr, ~ty: Type)
} derive (Show)


pub enum AsmTableLine {
  Data(~data: Array[TypedExpr], ~comment: Ref[String])
  Comment(String)
} derive (Show)

pub struct AsmTable {
  name: String
  offset: Int
  lines: Array[AsmTableLine]
  size: Int
  elem_size: @parse.ConstantSize
  items_count: Int
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
  Inst(~op: @parse.OpCode, ~mode: AsmAddressingMode, ~comment: String?)
  JumpEngine(~jump_table: Array[String])
  Comment(String)
} derive (Show)

pub struct BasicBlockLink {
  bb: BasicBlock
  is_fallthrough: Bool
} derive (Show)

pub struct BasicBlock {
  id: Int
  mut name: String?
  items: Array[AsmItem]
  predecessors: Array[BasicBlockLink]
  successors: Array[BasicBlockLink]
  mut is_subroutine_entry: Bool
  mut subroutine: AsmSubroutine?
} derive (Show)

pub struct AsmSubroutine {
  name: String
  blocks: Array[BasicBlock]
  entry: BasicBlock
  mut exit: BasicBlock
  internal_labels: Set[String]
} derive (Show)

pub fn ty(self: TypedExpr) -> Type {
  match self {
    Const(base=_, value=_, size=_, ~ty) => ty
    Ident(name=_, ~ty) => ty
    Unary(op=_, expr=_, ~ty) => ty
    Binary(op=_, lhs=_, rhs=_, ~ty) => ty
  }
}

pub fn suffix(self: AsmAddressingMode) -> String {
  match self {
    Accumulator => "_acc"
    Implied => ""
    Immediate(_) => "_imm"
    Relative(_) => "_rel"
    ZeroPage(_) => "_zp"
    ZeroPageX(_) => "_zpx"
    ZeroPageY(_) => "_zpy"
    Absolute(_) => "_abs"
    AbsoluteX(_) => "_absx"
    AbsoluteY(_) => "_absy"
    Indirect(_) => "_ind"
    IndexedIndirect(_) => "_indx"
    IndirectIndexed(_) => "_indy"
  }
}
