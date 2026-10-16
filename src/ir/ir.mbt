// Intermediate representation shared by the C and WebAssembly backends.
//
// A program is a set of functions, one per callable region the lowering
// recovered. Each function is a control-flow graph of blocks; statements read
// and write the machine state as explicit variables (registers and flags),
// single-assignment temporaries, and memory in one of three spaces:
//
//   Ram: an address proven to be in internal RAM ($0000-$07FF, after
//        masking mirrors), a plain array access;
//   Rom: an address proven to be in PRG ROM, a constant table read;
//   Bus: anything else (I/O, pointers), through the runtime's bus functions.
//
// Expressions have integer semantics: loads and 8-bit variables produce
// 0..255, flags 0 or 1, comparisons 0 or 1. Assigning to an 8-bit variable,
// an 8-bit temporary or a byte of memory keeps the low 8 bits.

pub(all) enum Var {
  A; X; Y; SP
  C; Z; N; V; I; D
} derive(Debug, Eq, Hash)

pub let all_vars : Array[Var] = [A, X, Y, SP, C, Z, N, V, I, D]

pub fn Var::bit(self : Var) -> Int {
  match self {
    A => 1; X => 2; Y => 4; SP => 8
    C => 16; Z => 32; N => 64; V => 128; I => 256; D => 512
  }
}

pub fn Var::is_flag(self : Var) -> Bool {
  match self {
    A | X | Y | SP => false
    _ => true
  }
}

// Names used by the generated C and in routine interfaces.
pub fn Var::c_name(self : Var) -> String {
  match self {
    A => "a"; X => "x"; Y => "y"; SP => "sp"
    C => "carry_flag"; Z => "zero_flag"; N => "neg_flag"; V => "overflow_flag"
    I => "interrupt_disabled"; D => "decimal_flag"
  }
}

pub fn Var::interface_name(self : Var) -> String {
  match self {
    C => "carry"; Z => "zero"; N => "negative"; V => "overflow"
    _ => self.c_name()
  }
}

pub const REGISTERS : Int = 7 // A, X, Y
pub const SP_BIT : Int = 8
pub const FLAGS : Int = 1008 // C, Z, N, V, I, D
pub const STATUS_FLAGS : Int = 240 // C, Z, N, V

pub fn vars_of(mask : Int) -> Array[Var] {
  all_vars.filter(fn(v) { (mask & v.bit()) != 0 })
}

pub(all) enum Mem {
  Ram; Rom; Bus
} derive(Debug, Eq)

pub(all) enum Width {
  U8; U16; Wide; Flag
} derive(Debug, Eq)

pub(all) enum UnOp {
  Not // logical: 1 when the operand is 0
  Lo // low byte
  Hi // high byte
  Byte // keep the low 8 bits
} derive(Debug, Eq)

pub(all) enum BinOp {
  Add; Sub; And; Or; Xor; Shl; Shr
  Eq; Ne; Lt; Ge // comparisons produce 0 or 1
} derive(Debug, Eq)

pub(all) enum Expr {
  Const(Int)
  Sym(String, Int) // a named constant and its value
  Var(Var)
  Temp(Int)
  Param // the dispatcher helper's return address
  Load(Mem, Expr)
  Unary(UnOp, Expr)
  Binary(BinOp, Expr, Expr)
} derive(Debug, Eq)

pub(all) enum Stmt {
  Assign(Var, Expr)
  Let(Int, Expr) // defines a temporary, once
  Store(Mem, Expr, Expr) // space, address, value
  // Hardware writes with fixed targets: apu_write(address, value),
  // ppu_bus_write(register, value), ppu_bus_oam_dma(value),
  // write_joypads(value).
  Effect(String, Array[Expr])
  Eval(Expr) // a read kept for its side effects
  Call(String)
} derive(Debug, Eq)

pub(all) enum Term {
  Goto(Int)
  Branch(Expr, Int, Int) // condition, taken, not taken
  Return
  ReturnValue(Expr) // dispatcher helpers return the jump target
  TailCall(String)
  // Call the dispatcher helper with the return address of the original
  // call, then continue in the routine at the address it returns.
  Dispatch(String, Int, Array[(Int, String)])
  Yield(Int) // idle until the next interrupt, resuming at this address
  Trap(Int) // an unproven transfer to this address
} derive(Debug, Eq)

pub(all) struct Block {
  label : String
  mut stmts : Array[Stmt]
  mut term : Term
} derive(Debug)

pub(all) struct Temp {
  hint : String
  width : Width
} derive(Debug)

pub(all) struct Function {
  name : String
  mut blocks : Array[Block]
  mut entry : Int
  temps : Array[Temp]
  doc : Array[String]
  native : Bool // body supplied by hand-written code; kept for analysis
  helper : Bool // dispatcher helper: takes the return address, returns a target
} derive(Debug)

pub(all) struct Program {
  functions : Array[Function]
  by_name : Map[String, Int]
  reset : String
  nmi : String
  prg : Array[Int] // the PRG ROM image, for folding constant reads
  // Filled by `analyze`.
  mut info : Analysis?
} derive(Debug)

pub fn Program::function(self : Program, name : String) -> Function? {
  match self.by_name.get(name) {
    Some(i) => Some(self.functions[i])
    None => None
  }
}

pub fn Program::roots(self : Program) -> Array[String] {
  if self.reset == self.nmi { [self.reset] } else { [self.reset, self.nmi] }
}

pub fn Function::new_temp(self : Function, hint : String, width : Width) -> Int {
  self.temps.push({ hint, width })
  self.temps.length() - 1
}

// Successor block indices of a terminator.
pub fn Term::successors(self : Term) -> Array[Int] {
  match self {
    Goto(b) => [b]
    Branch(_, t, f) => if t == f { [t] } else { [t, f] }
    _ => []
  }
}

pub fn Term::map_targets(self : Term, f : (Int) -> Int) -> Term {
  match self {
    Goto(b) => Goto(f(b))
    Branch(c, t, e) => Branch(c, f(t), f(e))
    other => other
  }
}

// Expression utilities

pub fn Expr::vars(self : Expr) -> Int {
  match self {
    Var(v) => v.bit()
    Load(_, e) | Unary(_, e) => e.vars()
    Binary(_, l, r) => l.vars() | r.vars()
    _ => 0
  }
}

pub fn Expr::uses_temp(self : Expr, t : Int) -> Int {
  match self {
    Temp(u) => if u == t { 1 } else { 0 }
    Load(_, e) | Unary(_, e) => e.uses_temp(t)
    Binary(_, l, r) => l.uses_temp(t) + r.uses_temp(t)
    _ => 0
  }
}

pub fn Expr::uses_var(self : Expr, v : Var) -> Int {
  match self {
    Var(u) => if u == v { 1 } else { 0 }
    Load(_, e) | Unary(_, e) => e.uses_var(v)
    Binary(_, l, r) => l.uses_var(v) + r.uses_var(v)
    _ => 0
  }
}

pub fn Expr::temps(self : Expr, out : Array[Int]) -> Unit {
  match self {
    Temp(t) => out.push(t)
    Load(_, e) | Unary(_, e) => e.temps(out)
    Binary(_, l, r) => { l.temps(out); r.temps(out) }
    _ => ()
  }
}

// Reads RAM or the bus (ROM never changes).
pub fn Expr::reads_memory(self : Expr) -> Bool {
  match self {
    Load(Rom, e) => e.reads_memory()
    Load(_, _) => true
    Unary(_, e) => e.reads_memory()
    Binary(_, l, r) => l.reads_memory() || r.reads_memory()
    _ => false
  }
}

// A bus read can have side effects (I/O registers).
pub fn Expr::is_pure(self : Expr) -> Bool {
  match self {
    Load(Bus, _) => false
    Load(_, e) | Unary(_, e) => e.is_pure()
    Binary(_, l, r) => l.is_pure() && r.is_pure()
    _ => true
  }
}

pub fn Expr::size(self : Expr) -> Int {
  match self {
    Load(_, e) | Unary(_, e) => 1 + e.size()
    Binary(_, l, r) => 1 + l.size() + r.size()
    _ => 1
  }
}

// Produces only 0 or 1.
pub fn Expr::is_boolean(self : Expr) -> Bool {
  match self {
    Var(v) => v.is_flag()
    Unary(Not, _) | Binary(Eq | Ne | Lt | Ge, _, _) => true
    _ => false
  }
}

pub fn Expr::is_atom(self : Expr) -> Bool {
  match self {
    Const(_) | Sym(_, _) | Var(_) | Temp(_) | Param => true
    _ => false
  }
}

pub fn Expr::replace_var(self : Expr, v : Var, by : Expr) -> Expr {
  match self {
    Var(u) if u == v => by
    Load(m, e) => Load(m, e.replace_var(v, by))
    Unary(op, e) => Unary(op, e.replace_var(v, by))
    Binary(op, l, r) => Binary(op, l.replace_var(v, by), r.replace_var(v, by))
    other => other
  }
}

pub fn Expr::replace_temp(self : Expr, t : Int, by : Expr) -> Expr {
  match self {
    Temp(u) if u == t => by
    Load(m, e) => Load(m, e.replace_temp(t, by))
    Unary(op, e) => Unary(op, e.replace_temp(t, by))
    Binary(op, l, r) => Binary(op, l.replace_temp(t, by), r.replace_temp(t, by))
    other => other
  }
}

// The value of an expression made only of constants.
pub fn Expr::value(self : Expr) -> Int? {
  match self {
    Const(v) | Sym(_, v) => Some(v)
    Unary(op, e) =>
      match e.value() {
        Some(v) => Some(unary(op, v))
        None => None
      }
    Binary(op, l, r) =>
      match (l.value(), r.value()) {
        (Some(a), Some(b)) => Some(binary(op, a, b))
        _ => None
      }
    _ => None
  }
}

fn unary(op : UnOp, v : Int) -> Int {
  match op {
    Not => if v == 0 { 1 } else { 0 }
    Lo | Byte => v & 0xff
    Hi => (v >> 8) & 0xff
  }
}

fn binary(op : BinOp, a : Int, b : Int) -> Int {
  match op {
    Add => a + b
    Sub => a - b
    And => a & b
    Or => a | b
    Xor => a ^ b
    Shl => a << b
    Shr => a >> b
    Eq => if a == b { 1 } else { 0 }
    Ne => if a != b { 1 } else { 0 }
    Lt => if a < b { 1 } else { 0 }
    Ge => if a >= b { 1 } else { 0 }
  }
}

pub(all) struct Range {
  lo : Int
  hi : Int
} derive(Debug, Eq)

let unknown : Range = { lo: -0x40000000, hi: 0x3fffffff }

fn smear(v : Int) -> Int {
  // smallest all-ones value covering v
  let mut m = 0
  while m < v { m = m * 2 + 1 }
  m
}

// Bounds of an expression's value; `temps` gives temporaries' widths.
pub fn Expr::range(self : Expr, temps : Array[Temp]) -> Range {
  match self {
    Const(v) | Sym(_, v) => { lo: v, hi: v }
    Var(v) => if v.is_flag() { { lo: 0, hi: 1 } } else { { lo: 0, hi: 255 } }
    Temp(t) if t < 0 || t >= temps.length() => unknown
    Temp(t) =>
      match temps[t].width {
        U8 => { lo: 0, hi: 255 }
        U16 => { lo: 0, hi: 0xffff }
        Flag => { lo: 0, hi: 1 }
        Wide => unknown
      }
    Param => { lo: 0, hi: 0xffff }
    Load(_, _) => { lo: 0, hi: 255 }
    Unary(Not, _) => { lo: 0, hi: 1 }
    Unary(_, _) => { lo: 0, hi: 255 }
    Binary(op, l, r) => {
      let a = l.range(temps)
      let b = r.range(temps)
      if a == unknown || b == unknown {
        return match op {
          Eq | Ne | Lt | Ge => { lo: 0, hi: 1 }
          And if b.lo >= 0 => { lo: 0, hi: b.hi }
          And if a.lo >= 0 => { lo: 0, hi: a.hi }
          _ => unknown
        }
      }
      match op {
        Add => { lo: a.lo + b.lo, hi: a.hi + b.hi }
        Sub => { lo: a.lo - b.hi, hi: a.hi - b.lo }
        And =>
          if a.lo >= 0 && b.lo >= 0 {
            { lo: 0, hi: if a.hi < b.hi { a.hi } else { b.hi } }
          } else if b.lo >= 0 {
            { lo: 0, hi: b.hi }
          } else if a.lo >= 0 {
            { lo: 0, hi: a.hi }
          } else {
            unknown
          }
        Or | Xor =>
          if a.lo >= 0 && b.lo >= 0 {
            { lo: 0, hi: smear(if a.hi > b.hi { a.hi } else { b.hi }) }
          } else {
            unknown
          }
        Shl =>
          if a.lo >= 0 && b.lo == b.hi && b.lo < 16 {
            { lo: a.lo << b.lo, hi: a.hi << b.lo }
          } else {
            unknown
          }
        Shr =>
          if a.lo >= 0 && b.lo == b.hi {
            { lo: a.lo >> b.lo, hi: a.hi >> b.lo }
          } else {
            unknown
          }
        Eq | Ne | Lt | Ge => { lo: 0, hi: 1 }
      }
    }
  }
}

pub fn Expr::fits_byte(self : Expr, temps : Array[Temp]) -> Bool {
  let r = self.range(temps)
  r.lo >= 0 && r.hi <= 255
}

// The value an assignment of `e` to a destination of this width stores.
pub fn Width::wrap(self : Width, e : Expr, temps : Array[Temp]) -> Expr {
  match self {
    U8 => if e.fits_byte(temps) { e } else { Unary(Byte, e) }
    U16 => {
      let r = e.range(temps)
      if r.lo >= 0 && r.hi <= 0xffff { e } else { Binary(And, e, Const(0xffff)) }
    }
    Wide | Flag => e
  }
}

pub fn Var::width(self : Var) -> Width {
  if self.is_flag() { Flag } else { U8 }
}

// Local algebraic simplification; keeps symbolic names where it can.
pub fn simplify(e : Expr) -> Expr {
  match e {
    Load(m, a) => Load(m, simplify(a))
    Unary(op, a) => {
      let a = simplify(a)
      match (op, a) {
        (_, Const(v)) => Const(unary(op, v))
        (Not, Unary(Not, inner)) if inner.is_boolean() => inner
        (Not, Binary(Eq, l, r)) => Binary(Ne, l, r)
        (Not, Binary(Ne, l, r)) => Binary(Eq, l, r)
        (Not, Binary(Lt, l, r)) => Binary(Ge, l, r)
        (Not, Binary(Ge, l, r)) => Binary(Lt, l, r)
        (Byte, inner) if inner.fits_byte([]) => inner
        // truncating inside a sum that is truncated again changes nothing
        (Byte, Binary(Add | Sub as bop, Unary(Byte, x), y)) => simplify(Unary(Byte, Binary(bop, x, y)))
        (Byte, Binary(Add | Sub as bop, x, Unary(Byte, y))) => simplify(Unary(Byte, Binary(bop, x, y)))
        _ => Unary(op, a)
      }
    }
    Binary(op, l, r) => {
      let l = simplify(l)
      let r = simplify(r)
      match (op, l, r) {
        (_, Const(a), Const(b)) => Const(binary(op, a, b))
        (Add | Sub | Or | Xor | Shl | Shr, x, Const(0)) => x
        // constant offsets combine: (x - 1) + 1, (x + 2) + 3
        (Add | Sub, Binary(Add | Sub as inner, x, Const(a)), Const(b)) => {
          let left = if inner == Add { a } else { -a }
          let total = if op == Add { left + b } else { left - b }
          if total == 0 {
            x
          } else if total > 0 {
            Binary(Add, x, Const(total))
          } else {
            Binary(Sub, x, Const(-total))
          }
        }
        (Add | Or | Xor, Const(0), x) => x
        // a flag compared with 0 or 1
        (Ne, Binary(Eq | Ne | Lt | Ge, _, _) as c, Const(0)) => c
        (Eq, Binary(Eq | Ne | Lt | Ge, _, _) as c, Const(0)) => simplify(Unary(Not, c))
        _ => Binary(op, l, r)
      }
    }
    other => other
  }
}

// Statement utilities

// Variables a statement defines (calls are handled by the analysis).
pub fn Stmt::defs(self : Stmt) -> Int {
  match self {
    Assign(v, _) => v.bit()
    _ => 0
  }
}

// Variables a statement reads (calls are handled by the analysis).
pub fn Stmt::uses(self : Stmt) -> Int {
  match self {
    Assign(_, e) | Let(_, e) | Eval(e) => e.vars()
    Store(_, a, v) => a.vars() | v.vars()
    Effect(_, args) => args.fold(init=0, fn(acc, e) { acc | e.vars() })
    Call(_) => 0
  }
}

pub fn Stmt::exprs(self : Stmt) -> Array[Expr] {
  match self {
    Assign(_, e) | Let(_, e) | Eval(e) => [e]
    Store(_, a, v) => [a, v]
    Effect(_, args) => args
    Call(_) => []
  }
}

pub fn Stmt::map_exprs(self : Stmt, f : (Expr) -> Expr) -> Stmt {
  match self {
    Assign(v, e) => Assign(v, f(e))
    Let(t, e) => Let(t, f(e))
    Eval(e) => Eval(f(e))
    Store(m, a, v) => Store(m, f(a), f(v))
    Effect(name, args) => Effect(name, args.map(f))
    Call(name) => Call(name)
  }
}

// Writes memory or performs I/O.
pub fn Stmt::has_effects(self : Stmt) -> Bool {
  match self {
    Store(_, _, _) | Effect(_, _) | Eval(_) | Call(_) => true
    Assign(_, e) | Let(_, e) => !e.is_pure()
  }
}

pub fn Term::exprs(self : Term) -> Array[Expr] {
  match self {
    Branch(c, _, _) => [c]
    ReturnValue(e) => [e]
    _ => []
  }
}

pub fn Term::map_exprs(self : Term, f : (Expr) -> Expr) -> Term {
  match self {
    Branch(c, t, e) => Branch(f(c), t, e)
    ReturnValue(e) => ReturnValue(f(e))
    other => other
  }
}
