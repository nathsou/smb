
pub(all) enum Type {
  ConstTy(@parse.ConstantSize)
  ArrayTy(@parse.ConstantSize)
  LabelTy
} derive (Debug, Eq)

pub(all) enum TypedExpr {
  Const(
    base~: @parse.ConstantBase,
    value~: Int,
    size~: @parse.ConstantSize,
    ty~: Type,
  )
  Ident(name~: String, ty~: Type)
  Unary(op~: @parse.UnaryOp, expr~: TypedExpr, ty~: Type)
  Binary(op~: @parse.BinaryOp, lhs~: TypedExpr, rhs~: TypedExpr, ty~: Type)
} derive (Debug)

pub(all) enum AsmTableLine {
  Data(data~: Array[TypedExpr], comment~: Ref[String])
  Comment(String)
} derive (Debug)

pub(all) struct AsmTable {
  name: String
  offset: Int
  lines: Array[AsmTableLine]
  size: Int
  elem_size: @parse.ConstantSize
  items_count: Int
} derive (Debug)

pub(all) enum AsmAddressingMode {
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
} derive (Debug)

pub(all) enum AsmItem {
  Label(String)
  Inst(id~: Int, op~: @parse.OpCode, mode~: AsmAddressingMode, comment~: String?)
  JumpEngine(jump_table~: Array[String])
  If(cond~: @parse.OpCode, then~: Array[AsmItem], otherwise~: Array[AsmItem]?, comment~: String?)
  Comment(String)
} derive (Debug)

pub(all) struct FlagLiveness {
  carry: Bool
  zero: Bool
  negative: Bool
} derive (Debug, Eq)

pub fn FlagLiveness::none() -> FlagLiveness {
  { carry: false, zero: false, negative: false }
}

pub fn FlagLiveness::all() -> FlagLiveness {
  { carry: true, zero: true, negative: true }
}

pub fn FlagLiveness::union(self: FlagLiveness, other: FlagLiveness) -> FlagLiveness {
  {
    carry: self.carry || other.carry,
    zero: self.zero || other.zero,
    negative: self.negative || other.negative,
  }
}

pub fn FlagLiveness::subtract(self: FlagLiveness, other: FlagLiveness) -> FlagLiveness {
  {
    carry: self.carry && !other.carry,
    zero: self.zero && !other.zero,
    negative: self.negative && !other.negative,
  }
}

pub fn FlagLiveness::intersect(self: FlagLiveness, other: FlagLiveness) -> FlagLiveness {
  {
    carry: self.carry && other.carry,
    zero: self.zero && other.zero,
    negative: self.negative && other.negative,
  }
}

pub fn FlagLiveness::is_none(self: FlagLiveness) -> Bool {
  !self.carry && !self.zero && !self.negative
}

pub(all) struct BasicBlockLink {
  bb: BasicBlock
  is_fallthrough: Bool
} derive (Debug)

pub(all) struct BasicBlock {
  id: Int
  mut name: String?
  items: Array[AsmItem]
  predecessors: Array[BasicBlockLink]
  successors: Array[BasicBlockLink]
  mut is_subroutine_entry: Bool
  mut subroutine: AsmSubroutine?
} derive (Debug)

pub(all) struct AsmSubroutine {
  name: String
  blocks: Array[BasicBlock]
  entry: BasicBlock
  mut exit: BasicBlock
  internal_labels: Set[String]
} derive (Debug)

// subroutines with a flat array of items instead of basic blocks
pub(all) struct AsmFlatSubroutine {
  name: String
  items: Array[AsmItem]
} derive (Debug)

pub fn TypedExpr::ty(self: TypedExpr) -> Type {
  match self {
    Const(base=_, value=_, size=_, ty~) => ty
    Ident(name=_, ty~) => ty
    Unary(op=_, expr=_, ty~) => ty
    Binary(op=_, lhs=_, rhs=_, ty~) => ty
  }
}

pub fn AsmAddressingMode::suffix(self: AsmAddressingMode) -> String {
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
