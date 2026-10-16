// Building the IR from the lowering's callable regions.
//
// Every instruction is translated with its complete semantics, every flag
// included: this unoptimised form is the reference the differential test
// compares the optimised program against. Dead flags and registers are
// removed later by `optimize`, using the IR's own interprocedural liveness.

pub suberror BuildError { BuildError(String) } derive(Debug)

priv enum Operand {
  Imm(Expr)
  At(Mem, Expr)
}

priv struct Builder {
  low : @lower.AsmLowering
  program : @image.Program
  f : Function
  stmts : Array[Stmt]
}

fn rename(label : String) -> String {
  if label.has_suffix("__sub") {
    label.split("__sub").iter().next().unwrap().to_owned()
  } else {
    label
  }
}

fn Builder::value_of(self : Builder, e : @lower.TypedExpr) -> Int? {
  match e {
    Const(value~, ..) => Some(value)
    Ident(name~, ..) =>
      match self.low.defines.get(name) {
        Some(d) => self.value_of(d)
        None => self.program.image.symbols.get(name)
      }
    Unary(op~, expr~, ..) =>
      match self.value_of(expr) {
        Some(v) =>
          Some(
            match op {
              GetHighByte => (v >> 8) & 0xff
              GetLowByte => v & 0xff
            },
          )
        None => None
      }
    Binary(op~, lhs~, rhs~, ..) =>
      match (self.value_of(lhs), self.value_of(rhs)) {
        (Some(a), Some(b)) =>
          Some(
            match op {
              Add => a + b
              Sub => a - b
            },
          )
        _ => None
      }
  }
}

fn Builder::expr(self : Builder, e : @lower.TypedExpr) -> Expr raise BuildError {
  match e {
    Const(value~, ..) => Const(value)
    Ident(name~, ty~) => {
      let value = match self.value_of(e) {
        Some(v) => v
        None => raise BuildError("Unknown symbol: \{name}")
      }
      match ty {
        LabelTy => Sym("ROM_ADDR_" + name, value)
        _ => Sym(name, value)
      }
    }
    Unary(op~, expr~, ..) =>
      Unary(
        match op {
          GetHighByte => Hi
          GetLowByte => Lo
        },
        self.expr(expr),
      )
    Binary(op~, lhs~, rhs~, ..) =>
      Binary(
        match op {
          Add => Add
          Sub => Sub
        },
        self.expr(lhs),
        self.expr(rhs),
      )
  }
}

// The zero-page address after `p`, wrapping within page zero.
fn zp_next(p : Expr) -> Expr {
  match p.value() {
    Some(v) if v + 1 <= 0xff => Binary(Add, p, Const(1))
    Some(_) => Const(0)
    None => Unary(Byte, Binary(Add, p, Const(1)))
  }
}

fn pointer(p : Expr) -> Expr {
  Binary(Or, Load(Ram, p), Binary(Shl, Load(Ram, zp_next(p)), Const(8)))
}

// An absolute address, classified by the range of values it can take.
fn absolute(addr : Expr, base : Int, indexed : Bool) -> Operand {
  let lo = base
  let hi = if indexed { base + 0xff } else { base }
  if lo >= 0 && hi < 0x800 {
    At(Ram, addr)
  } else if lo >= 0 && hi < 0x2000 {
    At(Ram, Binary(And, addr, Const(0x7ff)))
  } else if lo >= 0x8000 && hi < 0x10000 {
    At(Rom, addr)
  } else {
    At(Bus, addr)
  }
}

fn Builder::operand(self : Builder, mode : @lower.AsmAddressingMode) -> Operand raise BuildError {
  match mode {
    Immediate(e) => Imm(self.expr(e))
    ZeroPage(e) => At(Ram, self.expr(e))
    ZeroPageX(e) => At(Ram, Unary(Byte, Binary(Add, self.expr(e), Var(X))))
    ZeroPageY(e) => At(Ram, Unary(Byte, Binary(Add, self.expr(e), Var(Y))))
    Absolute(e) | AbsoluteX(e) | AbsoluteY(e) => {
      let base = self.expr(e)
      let value = match base.value() {
        Some(v) => v
        None => raise BuildError("Unknown address")
      }
      match mode {
        AbsoluteX(_) => absolute(Binary(Add, base, Var(X)), value, true)
        AbsoluteY(_) => absolute(Binary(Add, base, Var(Y)), value, true)
        _ => absolute(base, value, false)
      }
    }
    IndirectIndexed(e) => At(Bus, Binary(Add, pointer(self.expr(e)), Var(Y)))
    IndexedIndirect(e) =>
      At(Bus, pointer(Unary(Byte, Binary(Add, self.expr(e), Var(X)))))
    _ => raise BuildError("Operand expected")
  }
}

fn Builder::emit(self : Builder, s : Stmt) -> Unit {
  self.stmts.push(s)
}

fn Builder::temp(self : Builder, hint : String, width : Width, value : Expr) -> Expr {
  let t = self.f.new_temp(hint, width)
  self.emit(Let(t, value))
  Temp(t)
}

// The operand's value, bound to a temporary when reading it has effects.
fn Builder::read(self : Builder, operand : Operand) -> Expr {
  match operand {
    Imm(e) => e
    At(Bus, addr) => self.temp("m", U8, Load(Bus, addr))
    At(space, addr) => Load(space, addr)
  }
}

fn Builder::nz(self : Builder, value : Expr) -> Unit {
  self.emit(Assign(Z, Binary(Eq, value, Const(0))))
  self.emit(Assign(N, Binary(Ne, Binary(And, value, Const(0x80)), Const(0))))
}

fn Builder::store(self : Builder, mode : @lower.AsmAddressingMode, value : Expr) -> Unit raise BuildError {
  match self.operand(mode) {
    At(Ram, addr) => self.emit(Store(Ram, addr, value))
    At(_, addr) =>
      match addr.value() {
        Some(a) if a >= 0x2000 && a < 0x4000 =>
          self.emit(Effect("ppu_bus_write", [Const(0x2000 + (a & 7)), value]))
        Some(0x4014) => self.emit(Effect("ppu_bus_oam_dma", [value]))
        Some(0x4016) => self.emit(Effect("write_joypads", [value]))
        Some(a) if a >= 0x4000 && a < 0x4020 =>
          self.emit(Effect("apu_write", [addr, value]))
        _ => self.emit(Store(Bus, addr, value))
      }
    Imm(_) => raise BuildError("Store to an immediate")
  }
}

// A read-modify-write location: a register, RAM (read again after the
// write), or a bus address read once into a temporary.
priv enum Place {
  InVar(Var)
  InRam(Expr)
  OnBus(Expr) // address temporary
}

fn Builder::place(self : Builder, mode : @lower.AsmAddressingMode) -> Place raise BuildError {
  match mode {
    Accumulator => InVar(A)
    _ =>
      match self.operand(mode) {
        At(Ram, addr) => InRam(addr)
        At(_, addr) => OnBus(self.temp("address", U16, addr))
        Imm(_) => raise BuildError("Read-modify-write of an immediate")
      }
  }
}

// Applies `update` to the place and returns an expression for the new value.
// `carry_out` gives the carry from the old value; a rotate reads the old
// carry, so its new carry goes through a temporary.
fn Builder::modify(self : Builder, place : Place, update : (Expr) -> Expr, carry_out : ((Expr) -> Expr)?, rotates~ : Bool = false) -> Expr {
  match place {
    OnBus(address) => {
      let old = self.temp("m", U8, Load(Bus, address))
      let new = self.temp("m", U8, update(old))
      if carry_out is Some(out) { self.emit(Assign(C, out(old))) }
      self.emit(Store(Bus, address, new))
      new
    }
    _ => {
      let old = match place {
        InVar(v) => Var(v)
        InRam(addr) => Load(Ram, addr)
        OnBus(_) => abort("unreachable")
      }
      let write = fn(value) {
        match place {
          InVar(v) => self.emit(Assign(v, value))
          InRam(addr) => self.emit(Store(Ram, addr, value))
          OnBus(_) => ()
        }
      }
      match carry_out {
        Some(out) if rotates => {
          let c = self.temp("carry", Flag, out(old))
          write(update(old))
          self.emit(Assign(C, c))
        }
        Some(out) => {
          self.emit(Assign(C, out(old)))
          write(update(old))
        }
        None => write(update(old))
      }
      old
    }
  }
}

fn register_of(op : @parse.OpCode) -> Var {
  match op {
    LDX | CPX | INX | DEX | TAX | TSX | STX => X
    LDY | CPY | INY | DEY | TAY | STY => Y
    _ => A
  }
}

fn bit_set(value : Expr, mask : Int) -> Expr {
  Binary(Ne, Binary(And, value, Const(mask)), Const(0))
}

fn push(b : Builder, value : Expr) -> Unit {
  b.emit(Store(Ram, Binary(Add, Const(0x100), Var(SP)), value))
  b.emit(Assign(SP, Binary(Sub, Var(SP), Const(1))))
}

fn pull(b : Builder) -> Expr {
  b.emit(Assign(SP, Binary(Add, Var(SP), Const(1))))
  Load(Ram, Binary(Add, Const(0x100), Var(SP)))
}

// One non-control-flow instruction.
fn Builder::instruction(self : Builder, op : @parse.OpCode, mode : @lower.AsmAddressingMode) -> Unit raise BuildError {
  match op {
    LDA | LDX | LDY => {
      let r = register_of(op)
      self.emit(Assign(r, self.read(self.operand(mode))))
      self.nz(Var(r))
    }
    STA | STX | STY => self.store(mode, Var(register_of(op)))
    AND | ORA | EOR => {
      let m = self.read(self.operand(mode))
      let bop : BinOp = match op {
        AND => And
        ORA => Or
        _ => Xor
      }
      self.emit(Assign(A, Binary(bop, Var(A), m)))
      self.nz(Var(A))
    }
    ADC => {
      let m = self.read(self.operand(mode))
      let sum = self.temp("sum", U16, Binary(Add, Binary(Add, Var(A), m), Var(C)))
      self.emit(Assign(V, bit_set(Binary(And, Binary(Xor, Var(A), sum), Binary(Xor, m, sum)), 0x80)))
      self.emit(Assign(C, Binary(Ge, sum, Const(0x100))))
      self.emit(Assign(A, sum))
      self.nz(Var(A))
    }
    SBC => {
      let m = self.read(self.operand(mode))
      let diff = self.temp("diff", Wide, Binary(Sub, Binary(Sub, Var(A), m), Unary(Not, Var(C))))
      self.emit(Assign(V, bit_set(Binary(And, Binary(Xor, Var(A), m), Binary(Xor, Var(A), diff)), 0x80)))
      self.emit(Assign(C, Binary(Ge, diff, Const(0))))
      self.emit(Assign(A, diff))
      self.nz(Var(A))
    }
    CMP | CPX | CPY => {
      let r = Var(register_of(op))
      let m = self.read(self.operand(mode))
      self.emit(Assign(C, Binary(Ge, r, m)))
      self.emit(Assign(Z, Binary(Eq, r, m)))
      self.emit(Assign(N, bit_set(Binary(Sub, r, m), 0x80)))
    }
    BIT => {
      let m = self.read(self.operand(mode))
      self.emit(Assign(Z, Binary(Eq, Binary(And, Var(A), m), Const(0))))
      self.emit(Assign(N, bit_set(m, 0x80)))
      self.emit(Assign(V, bit_set(m, 0x40)))
    }
    TAX | TAY => { self.emit(Assign(register_of(op), Var(A))); self.nz(Var(register_of(op))) }
    TXA => { self.emit(Assign(A, Var(X))); self.nz(Var(A)) }
    TYA => { self.emit(Assign(A, Var(Y))); self.nz(Var(A)) }
    TSX => { self.emit(Assign(X, Var(SP))); self.nz(Var(X)) }
    TXS => self.emit(Assign(SP, Var(X)))
    INX | INY => {
      let r = register_of(op)
      self.emit(Assign(r, Binary(Add, Var(r), Const(1))))
      self.nz(Var(r))
    }
    DEX | DEY => {
      let r = register_of(op)
      self.emit(Assign(r, Binary(Sub, Var(r), Const(1))))
      self.nz(Var(r))
    }
    INC | DEC => {
      let bop : BinOp = if op == INC { Add } else { Sub }
      let value = self.modify(self.place(mode), fn(old) { Binary(bop, old, Const(1)) }, None)
      self.nz(value)
    }
    ASL | LSR | ROL | ROR => {
      let (update, out) : ((Expr) -> Expr, (Expr) -> Expr) = match op {
        ASL => (fn(old) { Binary(Shl, old, Const(1)) }, fn(old) { bit_set(old, 0x80) })
        LSR => (fn(old) { Binary(Shr, old, Const(1)) }, fn(old) { bit_set(old, 1) })
        ROL => (fn(old) { Binary(Or, Binary(Shl, old, Const(1)), Var(C)) }, fn(old) { bit_set(old, 0x80) })
        _ => (fn(old) { Binary(Or, Binary(Shr, old, Const(1)), Binary(Shl, Var(C), Const(7))) }, fn(old) { bit_set(old, 1) })
      }
      let value = self.modify(self.place(mode), update, Some(out), rotates=op == ROL || op == ROR)
      self.nz(value)
    }
    PHA => push(self, Var(A))
    PLA => {
      let value = pull(self)
      self.emit(Assign(A, value))
      self.nz(Var(A))
    }
    PHP => {
      let mut status = Const(0x30)
      for pair in [(C, 0), (Z, 1), (I, 2), (D, 3), (V, 6), (N, 7)] {
        let (flag, bit) = pair
        status = Binary(Or, status, if bit == 0 { Var(flag) } else { Binary(Shl, Var(flag), Const(bit)) })
      }
      push(self, status)
    }
    PLP => {
      let p = self.temp("p", U8, pull(self))
      for pair in [(C, 1), (Z, 2), (I, 4), (D, 8), (V, 0x40), (N, 0x80)] {
        let (flag, mask) = pair
        self.emit(Assign(flag, bit_set(p, mask)))
      }
    }
    CLC => self.emit(Assign(C, Const(0)))
    SEC => self.emit(Assign(C, Const(1)))
    CLD => self.emit(Assign(D, Const(0)))
    SED => self.emit(Assign(D, Const(1)))
    CLI => self.emit(Assign(I, Const(0)))
    SEI => self.emit(Assign(I, Const(1)))
    CLV => self.emit(Assign(V, Const(0)))
    NOP => ()
    _ => raise BuildError("Unsupported instruction")
  }
}

fn cond_of(op : @parse.OpCode) -> Expr {
  match op {
    BCC => Unary(Not, Var(C))
    BCS => Var(C)
    BEQ => Var(Z)
    BNE => Unary(Not, Var(Z))
    BMI => Var(N)
    BPL => Unary(Not, Var(N))
    BVS => Var(V)
    _ => Unary(Not, Var(V))
  }
}

fn new_function(name : String, doc : Array[String], native : Bool, helper : Bool) -> Function {
  { name, blocks: [], entry: 0, temps: [], doc, native, helper }
}

// Blocks for terminators reached by edges (returns, tail calls, traps).
priv struct Exits {
  f : Function
  tails : Map[String, Int]
  traps : Map[Int, Int]
  mut ret : Int?
}

fn Exits::ret(self : Exits) -> Int {
  match self.ret {
    Some(b) => b
    None => {
      self.f.blocks.push({ label: "", stmts: [], term: Return })
      self.ret = Some(self.f.blocks.length() - 1)
      self.f.blocks.length() - 1
    }
  }
}

fn Exits::tail(self : Exits, name : String) -> Int {
  match self.tails.get(name) {
    Some(b) => b
    None => {
      self.f.blocks.push({ label: "", stmts: [], term: TailCall(name) })
      self.tails[name] = self.f.blocks.length() - 1
      self.f.blocks.length() - 1
    }
  }
}

fn Exits::trap(self : Exits, pc : Int) -> Int {
  match self.traps.get(pc) {
    Some(b) => b
    None => {
      self.f.blocks.push({ label: "", stmts: [], term: Trap(pc) })
      self.traps[pc] = self.f.blocks.length() - 1
      self.f.blocks.length() - 1
    }
  }
}

fn hex4(v : Int) -> String {
  v.to_string(radix=16)
}

fn build_routine(
  low : @lower.AsmLowering,
  program : @image.Program,
  sub : @lower.AsmSubroutine,
  native : Bool,
) -> Function raise BuildError {
  let blocks = sub.blocks.copy()
  blocks.sort_by(fn(a, b) { a.id - b.id })
  let index : Map[Int, Int] = Map([])
  for i, bb in blocks { index[bb.id] = i }
  // Comments before the entry's first instruction document the routine.
  let doc = []
  for item in sub.entry.items {
    match item {
      Comment(text) => doc.push(text)
      Label(_) => ()
      _ => break
    }
  }
  let f = new_function(rename(sub.name), doc, native, false)
  for bb in blocks {
    let mut label = ""
    for item in bb.items {
      match item {
        Label(name) => { label = rename(name); break }
        Inst(id~, ..) if id >= 0 => { label = "loc_" + hex4(id); break }
        _ => ()
      }
    }
    f.blocks.push({ label, stmts: [], term: Return })
  }
  f.entry = index[sub.entry.id]
  let exits : Exits = { f, tails: Map([]), traps: Map([]), ret: None }
  fn target(label : String) -> Int raise BuildError {
    if label == "__ret__" { return exits.ret() }
    match low.bb_by_label.get(label) {
      Some(bb) if index.contains(bb.id) => index[bb.id]
      _ =>
        if low.is_subroutine_label(label) {
          exits.tail(rename(label))
        } else {
          raise BuildError("Jump outside the routine: \{label}")
        }
    }
  }
  fn fallthrough(bb : @lower.BasicBlock) -> Int raise BuildError {
    if index.get(bb.id + 1) is Some(i) { return i }
    match low.basic_blocks.get(bb.id + 1) {
      Some(next) =>
        for item in next.items {
          match item {
            Label(name) if low.is_subroutine_label(name) => return exits.tail(rename(name))
            _ => ()
          }
        }
      None => ()
    }
    raise BuildError("Execution falls out of \{sub.name}")
  }
  for i, bb in blocks {
    let builder : Builder = { low, program, f, stmts: [] }
    let mut term : Term? = None
    for item in bb.items {
      match item {
        Label(_) | Comment(_) => ()
        Inst(id~, op~, mode~, ..) =>
          match op {
            BCC | BCS | BEQ | BNE | BMI | BPL | BVC | BVS => {
              let label = match mode {
                Relative(Ident(name~, ..)) => name
                _ => raise BuildError("Unresolved branch")
              }
              term = Some(Branch(cond_of(op), target(label), fallthrough(bb)))
            }
            JMP =>
              if program.idle.contains(id) {
                term = Some(Yield(id))
              } else {
                match mode {
                  Absolute(Ident(name~, ..)) => term = Some(Goto(target(name)))
                  _ => raise BuildError("Unresolved jump")
                }
              }
            JSR =>
              match mode {
                Absolute(Ident(name~, ..)) => builder.emit(Call(rename(name)))
                _ => raise BuildError("Unresolved call")
              }
            RTS | RTI => term = Some(Return)
            BRK => raise BuildError("BRK is not supported")
            _ => builder.instruction(op, mode)
          }
        JumpEngine(jump_table~, site~) => {
          let dispatch = program.dispatches[site]
          let helper = @image.address_name(program.image, dispatch.helper)
          let cases = []
          let seen : Set[Int] = Set([])
          for k, label in jump_table {
            let address = dispatch.targets[k]
            if !seen.contains(address) {
              seen.add(address)
              cases.push((address, rename(label)))
            }
          }
          term = Some(Dispatch(helper, site + 2, cases))
        }
        _ => raise BuildError("Unexpected structured item")
      }
    }
    f.blocks[i].stmts = builder.stmts
    f.blocks[i].term = match term {
      Some(t) => t
      None => Goto(fallthrough(bb))
    }
  }
  f
}

// A dispatcher helper runs the setup the dispatch proof accepted, then
// returns the routine address it jumps to.
fn build_helper(
  low : @lower.AsmLowering,
  program : @image.Program,
  name : String,
  dispatch : @image.Dispatch,
) -> Function raise BuildError {
  let f = new_function(name, [], false, true)
  let exits : Exits = { f, tails: Map([]), traps: Map([]), ret: None }
  let mut builder : Builder = { low, program, f, stmts: [] }
  // The table is read through the return address of the original call.
  push(builder, Unary(Hi, Param))
  push(builder, Unary(Lo, Param))
  let finish = fn(builder : Builder, term : Term) -> Int {
    f.blocks.push({ label: "", stmts: builder.stmts, term })
    f.blocks.length() - 1
  }
  let pending : Array[(Int, Expr, Int)] = [] // block, condition, trap
  for step in dispatch.steps {
    let inst = step.inst
    if inst.op == JMP { continue }
    if inst.mode == "rel" {
      // Every accepted index took this direction; any other state traps.
      let other = if step.taken { inst.next } else { @image.branch_target(inst) }
      let cond = if step.taken { cond_of(inst.op) } else { simplify(Unary(Not, cond_of(inst.op))) }
      let b = finish(builder, Return)
      pending.push((b, cond, exits.trap(other)))
      builder = { low, program, f, stmts: [] }
      continue
    }
    let mode = low.lower_mode(@image.source_mode(program.image, inst), op=Some(inst.op)) catch {
      _ => raise BuildError("Invalid dispatcher instruction")
    }
    builder.instruction(inst.op, mode)
  }
  let target = match dispatch.exit {
    IndirectJump(pointer) => {
      let high = (pointer & 0xff00) | ((pointer + 1) & 0xff)
      let space : Mem = if pointer < 0x800 && high < 0x800 { Ram } else { Bus }
      Binary(Or, Load(space, Const(pointer)), Binary(Shl, Load(space, Const(high)), Const(8)))
    }
    ReturnJump => {
      let low_byte = builder.temp("low", U8, pull(builder))
      let high_byte = builder.temp("high", U8, pull(builder))
      Binary(And, Binary(Add, Binary(Or, low_byte, Binary(Shl, high_byte, Const(8))), Const(1)), Const(0xffff))
    }
  }
  let last = finish(builder, ReturnValue(target))
  // Chain the blocks split at proven branches.
  let mut next = last
  let mut k = pending.length() - 1
  while k >= 0 {
    let (b, cond, trap) = pending[k]
    f.blocks[b].term = Branch(cond, if b + 1 < f.blocks.length() && b + 1 != trap { b + 1 } else { next }, trap)
    next = b
    k -= 1
  }
  f.entry = if pending.is_empty() { last } else { pending[0].0 }
  f
}

// The whole program: dispatcher helpers, then one function per callable
// region. `native` routines keep their bodies for the analysis only.
pub fn build(low : @lower.AsmLowering, native~ : Set[String]) -> Program raise BuildError {
  let program = match low.program {
    Some(p) => p
    None => raise BuildError("The IR is built from a discovered program")
  }
  let functions = []
  let helpers : Map[Int, @image.Dispatch] = Map([])
  for dispatch in program.dispatches.values() { helpers[dispatch.helper] = dispatch }
  let addresses = helpers.keys().to_array()
  addresses.sort()
  for address in addresses {
    let name = @image.address_name(program.image, address)
    functions.push(build_helper(low, program, name, helpers[address]))
  }
  for sub in low.subroutines {
    functions.push(build_routine(low, program, sub, native.contains(rename(sub.name))))
  }
  let by_name : Map[String, Int] = Map([])
  for i, f in functions { by_name[f.name] = i }
  let base = if program.image.bytes.length() == 16384 { program.image.origin + 0x3ffa } else { 0xfffa }
  let nmi = try @image.address_name(program.image, program.image.word(base)) catch {
    _ => raise BuildError("Missing NMI vector")
  }
  { functions, by_name, reset: rename(program.roots[0]), nmi: rename(nmi), prg: program.image.bytes, info: None }
}
