// Registers, flags and scratch values as C locals and parameters
// (`--locals`).
//
// By default the generated C shares the machine's state between routines as
// the 6502 does: registers and flags are globals, and the scratch bytes
// $00-$07 are RAM. With `--locals`, each routine has its own registers and
// flags: what it reads of its caller's are its parameters, what its callers
// read after it returns are its results, and the rest are locals.
//
//   // in: x; out: a                       uint8_t lakitu_chase(uint8_t x) {
//   void lakitu_chase(void) {        ->       uint8_t a, y;
//     ...                                     ...
//
// The first result is returned, the others are written through pointers:
//
//   x = queue_metatile_tiles(x, y, &carry);
//
// A routine's results are its interface's outputs (src/ir/analysis.mbt:
// registers and flags it may change that a caller reads afterwards). Its
// parameters are what is live on its entry when its returns read its
// results: what it reads before writing, and the results it passes through
// unchanged on some path. They are a fixpoint over the program (a call reads
// its callee's parameters and sets its results).
//
// Scratch bytes stay in RAM between routines: SMB leaves values in them for
// the routines it calls and for its callers, mostly. A routine's own values
// in them, those that no call, entry or return takes part in, are its
// locals `t0` to `t7` (see `webs_of`), so `ram[0]` in the C is a value that
// crosses a routine's edge and `t0` one that does not.
//
// Some routines keep the machine's state: the roots, routines that wait for
// an interrupt, and routines the hand-written code calls that store through
// a pointer (`clear_memory`). They are printed as without `--locals`. A call from a
// routine with locals to one of them passes registers through the machine's
// (`*machine.y = y;`); a call from one of them to a routine with locals
// passes the globals.
//
// The hand-written routines (`--native`) have locals like the others, with
// the signatures their original code gives them (codegen/native/routines.h
// lists them, and those of the routines they call). What their original
// code does is not taken for what the C does: they may write any scratch
// byte and must write none, give nothing back as it was, may change any
// tracked byte, and the routines they call are entered with no equality.
//
// In routines with locals, accesses through pointers and indexed accesses
// from other bases are taken to stay out of the scratch bytes (SMB's
// pointers lead to ROM, the block buffers and the VRAM lists, and its
// indices stay within their arrays). `--check-scratch` builds the C for the
// tests with every such access checked (`scratch_guard`): the replay fails
// if one reaches a scratch byte that is a local there. In routines that keep
// the machine's state, any computed access may read or write any scratch
// byte (`clear_memory` clears them through a pointer).

// The registers and flags that become locals. SP stays global (the stack
// is memory), and so do the interrupt and decimal flags.
let local_vars : Int = 247 // A, X, Y, C, Z, N, V

// The scratch bytes $00-$07, as slots above the registers' bits.
let scratch_bytes : Int = 8

fn scratch_bit(k : Int) -> Int {
  1024 << k
}

let all_scratch : Int = 0x3fc00

// Every slot that can be a local.
let local_slots : Int = 0x3fcf7

priv enum Slot {
  Reg(@ir.Var)
  Scratch(Int)
}

fn slots_of(mask : Int) -> Array[Slot] {
  let out : Array[Slot] = @ir.vars_of(mask & local_vars).map(fn(v) { Reg(v) })
  for k in 0..<scratch_bytes {
    if (mask & scratch_bit(k)) != 0 { out.push(Scratch(k)) }
  }
  out
}

fn Slot::bit(self : Slot) -> Int {
  match self {
    Reg(v) => v.bit()
    Scratch(k) => scratch_bit(k)
  }
}

// Its name as a local and in interfaces.
fn Slot::local_name(self : Slot) -> String {
  match self {
    Reg(v) => v.interface_name()
    Scratch(k) => "t\{k}"
  }
}

// Its place in the machine's state.
fn Slot::machine_name(self : Slot) -> String {
  match self {
    Reg(v) => v.c_name()
    Scratch(k) => "ram[\{k}]"
  }
}

// Its place in the machine's state from a routine with locals, where the
// globals' names are taken by the locals.
fn Slot::machine_place(self : Slot) -> String {
  match self {
    Reg(v) => "*machine.\{v.interface_name()}"
    Scratch(k) => "ram[\{k}]"
  }
}

fn Slot::c_type(self : Slot) -> String {
  match self {
    Reg(v) if v.is_flag() => "bool"
    _ => "uint8_t"
  }
}

// The scratch byte a RAM address is, when it is one.
fn scratch_of(a : @ir.Expr) -> Int? {
  match a {
    Const(k) if k >= 0 && k < scratch_bytes => Some(k)
    _ => None
  }
}

// The constant part of an address.
fn constant_part(e : @ir.Expr) -> Int {
  match e {
    Const(v) | Sym(_, v) => v
    Binary(Add, l, r) => constant_part(l) + constant_part(r)
    Binary(Sub, l, Const(v)) => constant_part(l) - v
    _ => 0
  }
}

// The scratch bytes an address computed at run time may reach: those from
// its constant part, when that is a scratch byte (`ram[1 + y]`), and any
// when it then wraps within page zero.
fn reach(a : @ir.Expr) -> Int {
  if a.value() is Some(_) { return 0 }
  let (wraps, sum) = match a {
    Unary(Byte, inner) => (true, inner)
    Binary(And, inner, Const(0x7ff)) => (true, inner)
    _ => (false, a)
  }
  let base = constant_part(sum)
  if base < 0 || base >= scratch_bytes { return 0 }
  if wraps { return all_scratch }
  let mut bits = 0
  for k in base..<scratch_bytes { bits = bits | scratch_bit(k) }
  bits
}

// The scratch bytes a computed access may reach: from its base in a routine
// with locals, any in one that keeps the machine's state (`broad`).
fn reach_of(space : @ir.Mem, a : @ir.Expr, broad : Bool) -> Int {
  if a.value() is Some(_) { return 0 }
  // an address whose bounds keep it off them and their mirrors (the stack)
  let r = a.range([])
  let clear = r.lo >= scratch_bytes && (r.hi < 0x800 || r.lo >= 0x2000)
  match space {
    Ram | Bus if broad => if clear { 0 } else { all_scratch }
    Ram => reach(a)
    _ => 0
  }
}

// The slots an expression reads.
fn expr_slots(e : @ir.Expr, broad~ : Bool = false) -> Int {
  match e {
    Var(v) => v.bit()
    Load(Ram, a) if scratch_of(a) is Some(k) => scratch_bit(k)
    Load(space, a) => reach_of(space, a, broad) | expr_slots(a, broad~)
    Unary(_, a) => expr_slots(a, broad~)
    Binary(_, l, r) => expr_slots(l, broad~) | expr_slots(r, broad~)
    _ => 0
  }
}

// The slots a statement writes on every path through it.
fn stmt_defs(s : @ir.Stmt) -> Int {
  match s {
    Assign(v, _) => v.bit()
    Store(Ram, a, _) =>
      match scratch_of(a) {
        Some(k) => scratch_bit(k)
        None => 0
      }
    _ => 0
  }
}

// The slots a statement may write.
fn stmt_writes(s : @ir.Stmt, broad~ : Bool = false) -> Int {
  match s {
    Store(Ram, a, _) if scratch_of(a) is Some(_) => stmt_defs(s)
    Store(space, a, _) => reach_of(space, a, broad)
    _ => stmt_defs(s)
  }
}

// The slots a statement reads (calls are the convention's).
fn stmt_uses(s : @ir.Stmt, broad~ : Bool = false) -> Int {
  s.exprs().fold(init=0, fn(acc, e) { acc | expr_slots(e, broad~) })
}

fn term_uses(t : @ir.Term, broad~ : Bool = false) -> Int {
  t.exprs().fold(init=0, fn(acc, e) { acc | expr_slots(e, broad~) })
}

priv struct Signature {
  // Keeps the machine's state: every slot in it (globals and RAM).
  machine : Bool
  // The slots kept in the machine's state: all, or the scratch bytes the
  // routine indexes into.
  memory : Int
  // Live on entry: what the routine reads of its caller's slots, and the
  // results it may pass through unchanged.
  params : Int
  // What its callers read after it returns.
  outputs : Int
  // Registers and flags its callers read after it returns that it gives
  // back as they were: no results.
  kept : Int
  // The result returned; the others are written through pointers.
  primary : Slot?
}

// Results written through pointers.
fn Signature::pointer_bits(self : Signature) -> Int {
  let own = self.outputs & self.memory.lnot()
  match self.primary {
    Some(p) => own & p.bit().lnot()
    None => own
  }
}

// Parameters passed by value: those not written back through a pointer.
fn Signature::value_bits(self : Signature) -> Int {
  self.params & self.memory.lnot() & self.pointer_bits().lnot()
}

priv struct Convention {
  locals : Bool
  // Computed accesses checked against the scratch bytes that are locals
  // (`--check-scratch`, for the tests).
  mut check : Bool
  sigs : Map[String, Signature]
  // What waiting for an interrupt reads: the roots' demand, and the scratch
  // bytes the interrupt handlers read on entry.
  root : Int
  // The scratch values of each routine with locals that are locals.
  webs : Map[String, Webs]
  // What each routine is entered with equal, by every call (register slot,
  // RAM address), which what it gives back as it was may rest on.
  mut entered : Map[String, Array[(Int, Int)]]
  // The calls of each routine after which X holds what it held before
  // (block << 16 | statement): its result in X need not be assigned.
  mut same_x : Map[String, Set[Int]]
}

fn Convention::machine() -> Convention {
  { locals: false, check: false, sigs: Map([]), root: 0, webs: Map([]), entered: Map([]), same_x: Map([]) }
}

fn Convention::sig(self : Convention, name : String) -> Signature {
  match self.sigs.get(name) {
    Some(s) => s
    None => { machine: true, memory: local_slots, params: local_slots, outputs: local_slots, kept: 0, primary: None }
  }
}

// Whether `name` has locals in this program.
fn Convention::has_locals(self : Convention, name : String) -> Bool {
  self.locals && !self.sig(name).machine
}

// The slots `name` keeps in the machine's state.
fn Convention::memory(self : Convention, name : String) -> Int {
  if self.has_locals(name) { self.sig(name).memory } else { local_slots }
}

// The parameter that writes a result back: `out_y`, or `inout_y` when the
// routine also reads it.
fn pointer_name(sig : Signature, s : Slot) -> String {
  if (sig.params & s.bit()) != 0 { "inout_" + s.local_name() } else { "out_" + s.local_name() }
}

// What is live before a terminator: returns read the routine's results, a
// tail call its callee's parameters (and the results it leaves to the
// routine).
fn Convention::term_live(self : Convention, f : @ir.Function, term : @ir.Term, live : Array[Int]) -> Int {
  // (what the terminator reads, and the routine's own results, only where
  // they count)
  let reads = fn() { term_uses(term, broad=!self.has_locals(f.name)) }
  let outputs = fn() { self.sig(f.name).outputs }
  let tail = fn(g : String) {
    let s = self.sig(g)
    s.params | (outputs() & s.outputs.lnot())
  }
  match term {
    Goto(t) => live[t]
    Branch(_, t, e) => reads() | live[t] | live[e]
    Switch(_, _, _) => term.successors().fold(init=reads(), fn(acc, t) { acc | live[t] })
    Return => outputs()
    ReturnValue(_) => outputs() | reads()
    TailCall(g) => tail(g)
    Jump(_, cases) => cases.fold(init=reads(), fn(acc, c) { acc | tail(c.1) })
    Dispatch(h, _, cases) => {
      let helper = self.sig(h)
      let after = cases.fold(init=0, fn(acc, c) { acc | tail(c.1) })
      helper.params | (after & helper.outputs.lnot())
    }
    Suspend(t) => self.root | live[t]
    Yield(_) => self.root
    Trap(_) => 0
  }
}

// An assignment of a value with no side effects to a local that nothing
// reads: dropped, and what it reads is not live for it. (In the machine's
// state the value stayed, for a routine called later to read.)
fn dead(s : @ir.Stmt, after : Int, memory : Int) -> Bool {
  let bit = match s {
    Assign(_, e) | Store(Ram, _, e) if e.is_pure() => stmt_defs(s)
    _ => 0
  }
  bit != 0 && (bit & (local_slots & memory.lnot())) == bit && (after & bit) == 0
}

// What is live before a statement: a call reads its callee's parameters and
// sets its results.
fn Convention::stmt_live(self : Convention, s : @ir.Stmt, after : Int, memory : Int) -> Int {
  match s {
    Call(g) | CallValue(_, g, _) => {
      let sig = self.sig(g)
      sig.params | (after & sig.outputs.lnot())
    }
    _ if dead(s, after, memory) => after
    _ => (after & stmt_defs(s).lnot()) | stmt_uses(s, broad=memory == local_slots)
  }
}

// What is live on each block's entry.
fn block_live(f : @ir.Function, conv : Convention) -> Array[Int] {
  let memory = conv.memory(f.name)
  let live = f.blocks.map(fn(_) { 0 })
  let mut changed = true
  while changed {
    changed = false
    let mut b = f.blocks.length() - 1
    while b >= 0 {
      let block = f.blocks[b]
      let mut l = conv.term_live(f, block.term, live)
      let mut k = block.stmts.length() - 1
      while k >= 0 {
        l = conv.stmt_live(block.stmts[k], l, memory)
        k -= 1
      }
      l = l & local_slots
      if l != live[b] {
        live[b] = l
        changed = true
      }
      b -= 1
    }
  }
  live
}

// What is live after each statement of each block.
fn Convention::live_after(self : Convention, f : @ir.Function) -> Array[Array[Int]] {
  let memory = self.memory(f.name)
  let live = block_live(f, self)
  f.blocks.map(fn(block) {
    let after = block.stmts.map(fn(_) { 0 })
    let mut l = self.term_live(f, block.term, live) & local_slots
    let mut k = block.stmts.length() - 1
    while k >= 0 {
      after[k] = l
      l = self.stmt_live(block.stmts[k], l, memory) & local_slots
      k -= 1
    }
    after
  })
}

// The routines a hand-written routine's original code calls (the C calls
// them with the signatures that code gives them).
fn native_callees(program : @ir.Program) -> Set[String] {
  let out : Set[String] = Set([])
  for f in program.functions {
    if !f.native { continue }
    for block in f.blocks {
      for s in block.stmts {
        if s.callee() is Some(g) { out.add(g) }
      }
      for g in block.term.tail_calls() { out.add(g) }
      if block.term is Dispatch(h, _, _) { out.add(h) }
    }
  }
  out
}

// A store through a pointer, which may reach any byte: a routine the
// hand-written code calls that makes one keeps the machine's state
// (`clear_memory`, which clears RAM, the scratch bytes and the stack page
// included, through one).
fn stores_through_pointer(f : @ir.Function) -> Bool {
  f.blocks.iter().any(fn(block) { block.stmts.iter().any(fn(s) { s is Store(Bus, a, _) && a.value() is None }) })
}

fn waits(f : @ir.Function) -> Bool {
  !f.polls.is_empty() ||
  f.blocks.iter().any(fn(block) { block.term is (Yield(_) | Suspend(_)) })
}

// The routines a routine calls, tail calls or jumps to.
fn callees_of(f : @ir.Function) -> Array[String] {
  let out = []
  for block in f.blocks {
    for s in block.stmts {
      if s.callee() is Some(g) { out.push(g) }
    }
    out.append(block.term.tail_calls())
    if block.term is Dispatch(h, _, _) { out.push(h) }
  }
  out
}

// The scratch bytes each routine may write, with its callees'.
fn scratch_writes(program : @ir.Program, machine : Map[String, Bool]) -> Map[String, Int] {
  let writes : Map[String, Int] = Map([])
  for f in program.functions {
    let mut w = 0
    for block in f.blocks {
      for s in block.stmts { w = w | stmt_writes(s, broad=machine[f.name]) }
    }
    writes[f.name] = if f.native { all_scratch } else { w & all_scratch }
  }
  let mut changed = true
  while changed {
    changed = false
    for f in program.functions {
      let w = callees_of(f).fold(init=writes[f.name], fn(acc, g) { acc | writes.get(g).unwrap_or(all_scratch) })
      if w != writes[f.name] {
        writes[f.name] = w
        changed = true
      }
    }
  }
  writes
}

// The convention of each routine of `program` (analysed) with `--locals`:
// registers and flags are parameters, results and locals; scratch bytes
// stay in RAM between routines, and a routine's values in them that never
// leave it are locals (see `webs_of`).
fn Convention::locals_of(program : @ir.Program) -> Convention raise TranspileError {
  if !program.threads.is_empty() { raise LocalsUnsupported("coroutines (the map's threads)") }
  if !program.restarts.is_empty() { raise LocalsUnsupported("restarts") }
  let info = program.info.unwrap()
  let callees = native_callees(program)
  let machine : Map[String, Bool] = Map([])
  for f in program.functions {
    let m = program.is_root(f.name) || waits(f) || (callees.contains(f.name) && stores_through_pointer(f))
    if f.helper && (m || callees.contains(f.name)) { raise LocalsUnsupported("a dispatcher called by hand-written code: \{f.name}") }
    machine[f.name] = m
  }
  let writes = scratch_writes(program, machine)
  // What waiting for an interrupt reads: the roots' demand, and the scratch
  // bytes the interrupt handlers read on entry (from every byte down).
  let handlers = [program.nmi] + (match program.irq { Some(irq) => [irq]; None => [] })
  let root = (info.root_demand & local_vars) | all_scratch
  // results given back as they were: once is enough, since the registers'
  // results come from the interfaces, and calls already count those kept
  // as unchanged
  let kept : Map[String, Int] = Map(program.functions.map(fn(f) { (f.name, 0) }))
  let resolve = fn(kept : Map[String, Int], start : Int) -> (Convention, Int) {
    let mut root = start
    let mut conv = solve(program, info, machine, writes, root, kept)
    for ;; {
      let read = handlers.fold(init=0, fn(acc, h) { acc | conv.sig(h).params }) & all_scratch
      let next = (info.root_demand & local_vars) | read
      if next == root { break }
      root = next
      conv = solve(program, info, machine, writes, root, kept)
    }
    (conv, root)
  }
  let (first, root) = resolve(kept, root)
  let (found, entered, same_x) = kept_results(program, first, writes)
  let (conv, _) = resolve(found, root)
  conv.entered = entered
  conv.same_x = same_x
  let must = scratch_must_writes(program, conv)
  for f in program.functions {
    if conv.has_locals(f.name) { conv.webs[f.name] = conv.webs_of(f, writes, must) }
  }
  conv
}

// The signatures: registers' results from the interfaces, and what the
// scratch bytes hold for callers (in RAM): what call sites read after the
// call, of what the callee may write, until no call site reads more.
fn solve(
  program : @ir.Program,
  info : @ir.Analysis,
  machine : Map[String, Bool],
  writes : Map[String, Int],
  root : Int,
  kept : Map[String, Int],
) -> Convention {
  let demand : Map[String, Int] = Map(program.functions.map(fn(f) { (f.name, 0) }))
  // what reads each routine's parameters: its callers, and the routines
  // that tail call or jump to it
  let readers : Map[String, Array[String]] = Map([])
  for f in program.functions {
    for g in callees_of(f) {
      match readers.get(g) {
        Some(list) => if !list.contains(f.name) { list.push(f.name) }
        None => readers[g] = [f.name]
      }
    }
  }
  for ;; {
    let sigs : Map[String, Signature] = Map([])
    for f in program.functions {
      let m = machine[f.name]
      let memory = if m { local_slots } else { all_scratch }
      let registers = if program.is_root(f.name) { 0 } else { program.outputs(info, f.name) & local_vars }
      let outputs = (registers & kept[f.name].lnot()) | (writes[f.name] & demand[f.name])
      let primary = if f.helper || m { None } else { slots_of(outputs & memory.lnot()).get(0) }
      sigs[f.name] = { machine: m, memory, params: 0, outputs, kept: kept[f.name], primary }
    }
    let conv = { locals: true, check: false, sigs, root, webs: Map([]), entered: Map([]), same_x: Map([]) }
    // parameters: a least fixpoint, they only grow; a routine is followed
    // again when one it calls or goes to takes more
    let work = program.functions.map(fn(f) { f.name })
    let queued : Set[String] = Set::from_array(work)
    while work.pop() is Some(name) {
      queued.remove(name)
      let f = program.functions[program.by_name[name]]
      let entry = block_live(f, conv)[f.entry]
      let old = sigs[name]
      if (old.params | entry) != old.params {
        sigs[name] = { ..old, params: old.params | entry }
        for r in readers.get(name).unwrap_or([]) {
          if !queued.contains(r) { queued.add(r); work.push(r) }
        }
      }
    }
    // what each call site reads after the call
    let mut grew = false
    let note = fn(g : String, bits : Int) {
      let old = demand.get(g).unwrap_or(0)
      if (old | (bits & all_scratch)) != old {
        demand[g] = old | (bits & all_scratch)
        grew = true
      }
    }
    for f in program.functions {
      let after = conv.live_after(f)
      let own = sigs[f.name].outputs
      for b, block in f.blocks {
        for k, s in block.stmts {
          if s.callee() is Some(g) { note(g, after[b][k]) }
        }
        match block.term {
          TailCall(g) => note(g, own)
          Jump(_, cases) => for c in cases { note(c.1, own) }
          Dispatch(h, _, cases) => {
            let mut next = 0
            for c in cases {
              note(c.1, own)
              let s = conv.sig(c.1)
              next = next | s.params | (own & s.outputs.lnot())
            }
            note(h, next)
          }
          _ => ()
        }
      }
    }
    if !grew { return conv }
  }
}

// The parameters of a routine with locals, for its prototype.
fn Convention::parameters(self : Convention, f : @ir.Function) -> String {
  let sig = self.sig(f.name)
  let list = []
  if f.helper { list.push("uint16_t return_address") }
  for s in slots_of(sig.value_bits()) { list.push("\{s.c_type()} \{s.local_name()}") }
  for s in slots_of(sig.pointer_bits()) { list.push("\{s.c_type()} *\{pointer_name(sig, s)}") }
  if list.is_empty() { "void" } else { list.join(", ") }
}

fn Convention::return_type(self : Convention, f : @ir.Function) -> String {
  if f.helper { return "uint16_t" }
  match self.sig(f.name).primary {
    Some(s) => s.c_type()
    None => "void"
  }
}

fn Convention::prototype(self : Convention, f : @ir.Function) -> String {
  if !self.has_locals(f.name) { return "void \{f.name}(void)" }
  "\{self.return_type(f)} \{f.name}(\{self.parameters(f)})"
}

// The scratch bytes each routine writes on every path to its returns, with
// its callees' (a greatest fixpoint). Routines that keep the machine's
// state count as writing none: the hand-written ones may differ from the
// code they replace.
fn scratch_must_writes(program : @ir.Program, conv : Convention) -> Map[String, Int] {
  let must : Map[String, Int] = Map(program.functions.map(fn(f) {
    (f.name, if conv.has_locals(f.name) && !f.native { all_scratch } else { 0 })
  }))
  let get = fn(g : String) { must.get(g).unwrap_or(0) }
  let mut changed = true
  while changed {
    changed = false
    for f in program.functions {
      if !conv.has_locals(f.name) || f.native { continue }
      let preds : Array[Array[Int]] = f.blocks.map(fn(_) { [] })
      for b, block in f.blocks {
        for t in block.term.successors() { preds[t].push(b) }
      }
      let out = f.blocks.map(fn(_) { all_scratch })
      let mut inner = true
      while inner {
        inner = false
        for b, block in f.blocks {
          let mut w = if b == f.entry { 0 } else { preds[b].fold(init=all_scratch, fn(acc, p) { acc & out[p] }) }
          for s in block.stmts {
            w = w | (stmt_defs(s) & all_scratch)
            if s.callee() is Some(g) { w = w | get(g) }
          }
          if w != out[b] {
            out[b] = w
            inner = true
          }
        }
      }
      let mut all = all_scratch
      let cases_must = fn(cases : Array[(Int, String)]) {
        cases.fold(init=all_scratch, fn(acc, c) { acc & get(c.1) })
      }
      for b, block in f.blocks {
        match block.term {
          Return | ReturnValue(_) => all = all & out[b]
          TailCall(g) => all = all & (out[b] | get(g))
          Jump(_, cases) => all = all & (out[b] | cases_must(cases))
          Dispatch(h, _, cases) => all = all & (out[b] | get(h) | cases_must(cases))
          _ => ()
        }
      }
      if all != must[f.name] {
        must[f.name] = all
        changed = true
      }
    }
  }
  must
}

// The scratch values of a routine that are locals, by statement: bit k for
// byte $0k.
priv struct Webs {
  defs : Array[Array[Int]] // the write of the statement is to a local
  uses : Array[Array[Int]] // its reads are of a local
  terms : Array[Int] // the terminator's reads are of a local
  dead : Array[Array[Int]] // the write is to a local nothing reads
  locals : Int // the bytes with a local
}

// A disjoint-set forest over the writes and reads of a scratch byte.
priv struct Forest {
  parent : Array[Int]
  memory : Array[Bool] // the value is in RAM there: it crosses a call, the
  // routine's entry or return, or an index into the scratch bytes
  used : Array[Bool] // some read is in the set
}

fn Forest::add(self : Forest, memory : Bool, used : Bool) -> Int {
  self.parent.push(self.parent.length())
  self.memory.push(memory)
  self.used.push(used)
  self.parent.length() - 1
}

fn Forest::find(self : Forest, n : Int) -> Int {
  let mut r = n
  while self.parent[r] != r { r = self.parent[r] }
  self.parent[n] = r
  r
}

fn Forest::union(self : Forest, a : Int, b : Int) -> Unit {
  let ra = self.find(a)
  let rb = self.find(b)
  if ra == rb { return }
  self.parent[rb] = ra
  self.memory[ra] = self.memory[ra] || self.memory[rb]
  self.used[ra] = self.used[ra] || self.used[rb]
}

// Each scratch byte's webs in a routine with locals: its writes joined with
// the reads they reach. A web whose writes and reads are all the routine's
// own accesses of the byte is a local; one that a call, the entry, a return
// or an index into the scratch bytes takes part in stays in RAM (a call that
// may write the byte joins what it reaches, one that must write it ends what
// came before).
fn Convention::webs_of(self : Convention, f : @ir.Function, writes : Map[String, Int], must : Map[String, Int]) -> Webs {
  let own = self.sig(f.name)
  let n = f.blocks.length()
  let webs : Webs = {
    defs: f.blocks.map(fn(b) { b.stmts.map(fn(_) { 0 }) }),
    uses: f.blocks.map(fn(b) { b.stmts.map(fn(_) { 0 }) }),
    terms: f.blocks.map(fn(_) { 0 }),
    dead: f.blocks.map(fn(b) { b.stmts.map(fn(_) { 0 }) }),
    locals: 0,
  }
  let preds : Array[Array[Int]] = f.blocks.map(fn(_) { [] })
  for b, block in f.blocks {
    for t in block.term.successors() { preds[t].push(b) }
  }
  let mut locals = 0
  for k in 0..<scratch_bytes {
    let bit = scratch_bit(k)
    let forest : Forest = { parent: [], memory: [], used: [] }
    let entry = forest.add(true, false)
    // the node of each statement's write and read of the byte, -1 for none
    let def_node = f.blocks.map(fn(b) { b.stmts.map(fn(_) { -1 }) })
    let use_node = f.blocks.map(fn(b) { b.stmts.map(fn(_) { -1 }) })
    let term_node = f.blocks.map(fn(_) { -1 })
    for b, block in f.blocks {
      for i, s in block.stmts {
        let (reads, in_ram, write, kills, write_ram) = match s {
          Call(g) | CallValue(_, g, _) => {
            let sig = self.sig(g)
            let r = (sig.params & bit) != 0
            (r, r, (writes.get(g).unwrap_or(all_scratch) & bit) != 0, (must.get(g).unwrap_or(0) & bit) != 0, true)
          }
          _ => {
            let own_read = (stmt_uses(s) & bit) != 0
            let indexed = s.exprs().iter().any(fn(e) { (expr_slots(e) & bit) != 0 && !reads_own(e, k) })
            let own_write = (stmt_defs(s) & bit) != 0
            let computed = (stmt_writes(s) & bit) != 0 && !own_write
            (own_read, indexed, own_write || computed, own_write, computed)
          }
        }
        if reads { use_node[b][i] = forest.add(in_ram, true) }
        if write { def_node[b][i] = forest.add(write_ram, false) }
        ignore(kills)
      }
      let t = block.term
      let tail = fn(g : String) { ((self.sig(g).params | own.outputs) & bit) != 0 }
      let (reads, in_ram) = match t {
        Return | ReturnValue(_) => {
          let r = (own.outputs & bit) != 0
          (r || (term_uses(t) & bit) != 0, r)
        }
        TailCall(g) => (tail(g), tail(g))
        Jump(_, cases) => {
          let r = cases.iter().any(fn(c) { tail(c.1) })
          (r || (term_uses(t) & bit) != 0, r)
        }
        Dispatch(h, _, cases) => {
          let r = ((self.sig(h).params & bit) != 0) || cases.iter().any(fn(c) { tail(c.1) })
          (r, r)
        }
        Suspend(_) | Yield(_) => (true, true)
        _ => ((term_uses(t) & bit) != 0, false)
      }
      if reads { term_node[b] = forest.add(in_ram, true) }
    }
    // reaching writes: sets of nodes, forward to a fixpoint
    let out : Array[Set[Int]] = f.blocks.map(fn(_) { Set([]) })
    let flow = fn(b : Int, join : Bool) -> Set[Int] {
      let reach : Set[Int] = Set([])
      if b == f.entry { reach.add(entry) }
      for p in preds[b] {
        for d in out[p] { reach.add(d) }
      }
      let block = f.blocks[b]
      let mut current = reach
      for i, s in block.stmts {
        if join && use_node[b][i] >= 0 {
          for d in current { forest.union(use_node[b][i], d) }
        }
        if def_node[b][i] >= 0 {
          let kills = match s {
            Call(g) | CallValue(_, g, _) => (must.get(g).unwrap_or(0) & bit) != 0
            _ => (stmt_defs(s) & bit) != 0
          }
          if kills { current = Set([]) } else { current = Set::from_iter(current.iter()) }
          current.add(def_node[b][i])
        }
      }
      if join && term_node[b] >= 0 {
        for d in current { forest.union(term_node[b], d) }
      }
      current
    }
    let mut changed = true
    while changed {
      changed = false
      for b in 0..<n {
        let next = flow(b, false)
        if next.length() != out[b].length() {
          out[b] = next
          changed = true
        }
      }
    }
    for b in 0..<n { flow(b, true) |> ignore }
    // the webs that are locals
    let is_local = fn(node : Int) { node >= 0 && !forest.memory[forest.find(node)] }
    for b, block in f.blocks {
      for i, s in block.stmts {
        if s.callee() is Some(_) { continue }
        if is_local(def_node[b][i]) && (stmt_defs(s) & bit) != 0 {
          webs.defs[b][i] = webs.defs[b][i] | (1 << k)
          locals = locals | (1 << k)
          if !forest.used[forest.find(def_node[b][i])] { webs.dead[b][i] = webs.dead[b][i] | (1 << k) }
        }
        if is_local(use_node[b][i]) { webs.uses[b][i] = webs.uses[b][i] | (1 << k) }
      }
      if is_local(term_node[b]) { webs.terms[b] = webs.terms[b] | (1 << k) }
    }
  }
  { ..webs, locals }
}

// Whether an expression's reads of byte $0k are all its own (a constant
// address), with no index that may reach it.
fn reads_own(e : @ir.Expr, k : Int) -> Bool {
  match e {
    Load(Ram, a) if scratch_of(a) is Some(_) => true
    Load(space, a) => (reach_of(space, a, false) & scratch_bit(k)) == 0 && reads_own(a, k)
    Unary(_, a) => reads_own(a, k)
    Binary(_, l, r) => reads_own(l, k) && reads_own(r, k)
    _ => true
  }
}

// Results a routine gives back as they were on its entry: the registers
// and flags that every return holds the entry value of. They are no results
// (the caller keeps its own), though the routine may change them on the way
// (X used to walk an array, then reloaded from `slot`).
//
// A forward analysis tracks which entry value each register, flag, scratch
// byte, pushed stack slot and RAM byte that the code loads into X or Y
// holds: a copy keeps it, a call keeps those the callee gives back as they
// were or does not change, a push and a pull move it (calls leave the
// caller's pushed slots alone). A routine may also be known, from all of its
// call sites, to be entered with a register equal to a RAM byte (X to
// `slot`). Both are greatest fixpoints over the program.
//
// RAM bytes are taken to change only by their own stores, indexed stores
// that may reach them without wrapping around page zero or RAM (an index
// from a scratch byte stays within them), pointer stores into them above
// the stack page (anywhere in routines that keep the machine's state), and
// calls that may do one of these. `--check-scratch` builds check each of
// these in the generated routines (see `guard_failed` in code.h).
// Locations whose equalities are followed: registers and flags (their
// slot, 0-6), their entry values (0x100 + slot), scratch bytes (0x200 + k),
// tracked RAM bytes (0x1000 + address), pushed stack slots (0x10000 +
// offset from the entry SP, which may be negative) and constants (0x100000
// + value). The facts are a partition: locations in one class are equal.
let entry_base : Int = 0x100

let scratch_base : Int = 0x200

let byte_base : Int = 0x1000

let stack_base : Int = 0x10000

let constant_base : Int = 0x100000

priv struct Held {
  class : Map[Int, Int] // location -> its class (a member); absent: alone
  sp : Int? // the offset of SP from its entry value, when known
}

let held_vars : Array[@ir.Var] = [A, X, Y, C, Z, N, V]

fn held_index(v : @ir.Var) -> Int {
  match v {
    A => 0
    X => 1
    Y => 2
    C => 3
    Z => 4
    N => 5
    V => 6
    _ => -1
  }
}

fn Held::entry() -> Held {
  let class : Map[Int, Int] = Map([])
  for i in 0..<7 {
    class[i] = i
    class[entry_base + i] = i
  }
  { class, sp: Some(0) }
}

fn Held::copy(self : Held) -> Held {
  let class : Map[Int, Int] = Map([], capacity=self.class.length())
  for k, r in self.class { class[k] = r }
  { class, sp: self.sp }
}

fn Held::rep(self : Held, loc : Int) -> Int {
  self.class.get(loc).unwrap_or(loc)
}

fn Held::same(self : Held, a : Int, b : Int) -> Bool {
  a == b || self.rep(a) == self.rep(b)
}

// `loc` on its own: its class, if it named it, is renamed for another member.
fn Held::separate(self : Held, loc : Int) -> Unit {
  guard self.class.get(loc) is Some(r) else { return }
  self.class.remove(loc)
  if r != loc { return }
  let members = []
  for k, r in self.class {
    if r == loc { members.push(k) }
  }
  if members.length() == 1 {
    self.class.remove(members[0])
  } else if !members.is_empty() {
    let next = members[0]
    for m in members { self.class[m] = next }
  }
}

// `loc` now equal to `other` (and nothing else).
fn Held::assign(self : Held, loc : Int, other : Int?) -> Unit {
  self.separate(loc)
  match other {
    Some(o) if o != loc => {
      let r = self.rep(o)
      if !self.class.contains(o) { self.class[o] = o }
      self.class[loc] = r
    }
    _ => ()
  }
}

// `a` and `b` and their classes equal.
fn Held::union(self : Held, a : Int, b : Int) -> Unit {
  let ra = self.rep(a)
  let rb = self.rep(b)
  if ra == rb { return }
  // (a location alone is not in the map, and no member names it)
  if !self.class.contains(b) {
    if !self.class.contains(a) { self.class[a] = a }
    self.class[b] = ra
    return
  }
  if !self.class.contains(a) {
    self.class[a] = rb
    return
  }
  let members = []
  for k, r in self.class {
    if r == rb { members.push(k) }
  }
  for m in members { self.class[m] = ra }
  self.class[b] = ra
}

fn Held::forget(self : Held, keep : (Int) -> Bool) -> Unit {
  let gone = []
  for l, _ in self.class {
    if !keep(l) { gone.push(l) }
  }
  for l in gone { self.separate(l) }
}

// Locations equal in both.
fn Held::meet(self : Held, other : Held) -> Held {
  let groups : Map[(Int, Int), Array[Int]] = Map([])
  let keys : Set[Int] = Set([])
  for k, _ in self.class { keys.add(k) }
  for k, _ in other.class { keys.add(k) }
  for k in keys {
    let key = (self.rep(k), other.rep(k))
    match groups.get(key) {
      Some(list) => list.push(k)
      None => groups[key] = [k]
    }
  }
  let class : Map[Int, Int] = Map([])
  for _, members in groups {
    if members.length() < 2 { continue }
    let r = members.fold(init=members[0], fn(m, x) { if x < m { x } else { m } })
    for m in members { class[m] = r }
  }
  { class, sp: if self.sp == other.sp { self.sp } else { None } }
}

fn Held::canonical(self : Held) -> Map[Int, Int] {
  let least : Map[Int, Int] = Map([])
  for k, r in self.class {
    match least.get(r) {
      Some(m) if m <= k => ()
      _ => least[r] = k
    }
  }
  Map::from_iter(self.class.iter().map(fn(e) { (e.0, least[e.1]) }))
}

fn Held::equals(self : Held, other : Held) -> Bool {
  if self.sp != other.sp { return false }
  let a = self.canonical()
  let b = other.canonical()
  a.length() == b.length() && a.iter().all(fn(e) { b.get(e.0) == Some(e.1) })
}

// The stack slot an address is, as a location.
fn Held::slot_of(self : Held, a : @ir.Expr) -> Int? {
  match (a, self.sp) {
    (Binary(Add, Const(0x100), Var(SP)), Some(d)) => Some(stack_base + d)
    _ => None
  }
}

// The location an expression is, when it is one.
fn Held::location(self : Held, e : @ir.Expr, tracked : Set[Int]) -> Int? {
  match e {
    Const(c) | Sym(_, c) if c >= 0 && c < 256 => Some(constant_base + c)
    Var(v) if held_index(v) >= 0 => Some(held_index(v))
    Load(Ram, a) =>
      match (scratch_of(a), a.value(), self.slot_of(a)) {
        (Some(k), _, _) => Some(scratch_base + k)
        (_, Some(addr), _) if tracked.contains(addr) => Some(byte_base + addr)
        (_, _, Some(slot)) => Some(slot)
        _ => None
      }
    _ => None
  }
}

// The tracked RAM bytes a store may write: its own, those an index may
// reach without wrapping around page zero, those a pointer may reach above
// the stack page.
fn store_reach(space : @ir.Mem, a : @ir.Expr, tracked : Set[Int], broad~ : Bool = false) -> Array[Int] {
  match a.value() {
    Some(addr) => if space == Ram && tracked.contains(addr) { [addr] } else { [] }
    None => {
      let (lo, hi) = match (space, a) {
        (Ram, Unary(Byte, inner)) => (constant_part(inner), 0xff)
        (Ram, Binary(And, inner, Const(0x7ff))) => (constant_part(inner), 0x7ff)
        // an index from a scratch byte stays within them
        (Ram, _) if constant_part(a) >= 0 && constant_part(a) < scratch_bytes => (1, 0)
        (Ram, _) => {
          let r = a.range([])
          (r.lo, r.hi)
        }
        // (in routines that keep the machine's state, anywhere: clear_memory)
        (Bus, _) => if broad { (0, 0x7ff) } else { (0x200, 0x7ff) }
        _ => (1, 0)
      }
      tracked.iter().filter(fn(t) { t >= lo && t <= hi }).collect()
    }
  }
}

priv struct Facts {
  conv : Convention
  writes : Map[String, Int] // scratch bytes each routine may write
  tracked : Set[Int] // the RAM bytes followed
  changes : Map[String, Set[Int]] // the tracked bytes each routine may write
  kept : Map[String, Int] // what each routine gives back as it was
  equal : Map[String, Array[(Int, Int)]] // (register slot, address): equal on entry
  exits : Map[String, Array[(Int, Int)]] // (register slot, address): equal on every return
}

// The facts after `s`: `h` itself, updated (copy it to keep it).
fn Facts::step(self : Facts, h : Held, s : @ir.Stmt) -> Held {
  let out = h
  let is_stack = fn(l : Int) { l >= stack_base && l < constant_base }
  match s {
    Assign(SP, Binary(Add | Sub as op, Var(SP), Const(c))) =>
      { ..out, sp: h.sp.map(fn(d) { if op == Add { d + c } else { d - c } }) }
    Assign(SP, _) => {
      out.forget(fn(l) { !is_stack(l) })
      { ..out, sp: None }
    }
    Assign(v, e) => {
      if held_index(v) >= 0 { out.assign(held_index(v), h.location(e, self.tracked)) }
      out
    }
    Store(space, a, value) => {
      let v = h.location(value, self.tracked)
      match (space, scratch_of(a), h.slot_of(a)) {
        (Ram, Some(k), _) => out.assign(scratch_base + k, v)
        (Ram, _, Some(slot)) => out.assign(slot, v)
        _ => {
          let reached = store_reach(space, a, self.tracked)
          match a.value() {
            Some(addr) if reached.length() == 1 => out.assign(byte_base + addr, v)
            _ => for t in reached { out.separate(byte_base + t) }
          }
          // an index that may reach the stack page
          if space == Ram && a.value() is None {
            let r = a.range([])
            if !(r.lo >= 0x200 || r.hi < 0x100) { out.forget(fn(l) { !is_stack(l) }) }
          }
        }
      }
      out
    }
    Call(g) | CallValue(_, g, _) => {
      let sig = self.conv.sig(g)
      let same = self.kept.get(g).unwrap_or(0)
      for v in held_vars {
        if (sig.outputs & v.bit()) != 0 && (same & v.bit()) == 0 { out.separate(held_index(v)) }
      }
      let w = self.writes.get(g).unwrap_or(all_scratch)
      for k in 0..<scratch_bytes {
        if (w & scratch_bit(k)) != 0 { out.separate(scratch_base + k) }
      }
      match self.changes.get(g) {
        Some(set) => for t in set { out.separate(byte_base + t) }
        None => for t in self.tracked { out.separate(byte_base + t) }
      }
      // what the callee leaves equal on return (X reloaded from `slot`)
      for pair in self.exits.get(g).unwrap_or([]) { out.union(pair.0, byte_base + pair.1) }
      // the callee uses the stack below the caller's pushes
      match h.sp {
        Some(d) if !(s is CallValue(_, _, _)) => out.forget(fn(l) { !is_stack(l) || l - stack_base > d })
        _ => out.forget(fn(l) { !is_stack(l) })
      }
      out
    }
    _ => out
  }
}

// The facts at the end of each block of `f`, None where not reached.
fn Facts::flow(self : Facts, f : @ir.Function) -> (Array[Held?], Array[Held?]) {
  let preds : Array[Array[Int]] = f.blocks.map(fn(_) { [] })
  for b, block in f.blocks {
    for t in block.term.successors() { preds[t].push(b) }
  }
  let entry = Held::entry()
  for pair in self.equal.get(f.name).unwrap_or([]) { entry.union(pair.0, byte_base + pair.1) }
  let starts : Array[Held?] = f.blocks.map(fn(_) { None })
  let ends : Array[Held?] = f.blocks.map(fn(_) { None })
  // in reverse postorder from the entry, a block again only when what one
  // before it ends with changed
  let order = reverse_postorder(f)
  let dirty = f.blocks.map(fn(_) { true })
  let mut moving = true
  while moving {
    moving = false
    for b in order {
      if !dirty[b] { continue }
      dirty[b] = false
      let block = f.blocks[b]
      let joined = preds[b].fold(init=None, fn(acc : Held?, p) {
        match (acc, ends[p]) {
          (None, e) => e
          (Some(a), Some(e)) => Some(a.meet(e))
          (a, None) => a
        }
      })
      let start = if b == f.entry {
        match joined {
          Some(j) => Some(j.meet(entry))
          None => Some(entry)
        }
      } else {
        joined
      }
      guard start is Some(facts) else { continue }
      starts[b] = Some(facts)
      let mut h = facts.copy()
      for s in block.stmts { h = self.step(h, s) }
      let same = match ends[b] {
        Some(e) => e.equals(h)
        None => false
      }
      if !same {
        ends[b] = Some(h)
        for t in block.term.successors() {
          dirty[t] = true
          moving = true
        }
      }
    }
  }
  (starts, ends)
}

// The blocks reached from the entry, each before those it leads to (but
// along back edges).
fn reverse_postorder(f : @ir.Function) -> Array[Int] {
  let seen = f.blocks.map(fn(_) { false })
  let post = []
  // an explicit stack: (block, next successor to visit)
  let stack : Array[(Int, Int)] = [(f.entry, 0)]
  seen[f.entry] = true
  while stack.length() > 0 {
    let (b, i) = stack[stack.length() - 1]
    let succ = f.blocks[b].term.successors()
    if i < succ.length() {
      stack[stack.length() - 1] = (b, i + 1)
      let t = succ[i]
      if !seen[t] {
        seen[t] = true
        stack.push((t, 0))
      }
    } else {
      post.push(b)
      ignore(stack.pop())
    }
  }
  post.rev()
}

// What a register slot holds at a tail call of `g`, kept: the entry value.
fn Facts::gives(self : Facts, h : Held, g : String) -> Int {
  let s = self.conv.sig(g)
  let same = self.kept.get(g).unwrap_or(0)
  let mut bits = 0
  for v in held_vars {
    let untouched = (s.outputs & v.bit()) == 0 || (same & v.bit()) != 0
    if untouched && h.same(held_index(v), entry_base + held_index(v)) { bits = bits | v.bit() }
  }
  bits
}

// The results each routine with locals gives back as they were.
fn kept_results(
  program : @ir.Program,
  conv : Convention,
  writes : Map[String, Int],
) -> (Map[String, Int], Map[String, Array[(Int, Int)]], Map[String, Set[Int]]) {
  // the RAM bytes code with locals loads into index registers (`slot`)
  let tracked : Set[Int] = Set([])
  for f in program.functions {
    if !conv.has_locals(f.name) || f.native { continue }
    for block in f.blocks {
      for s in block.stmts {
        match s {
          Assign(X | Y, Load(Ram, address)) =>
            match address.value() {
              Some(a) if a >= scratch_bytes && a < 0x800 => tracked.add(a)
              _ => ()
            }
          _ => ()
        }
      }
    }
  }
  // the tracked bytes each routine may write, with its callees'
  let changes : Map[String, Set[Int]] = Map([])
  for f in program.functions {
    if f.native { continue }
    let set : Set[Int] = Set([])
    for block in f.blocks {
      for s in block.stmts {
        if s is Store(space, a, _) {
          for t in store_reach(space, a, tracked, broad=!conv.has_locals(f.name)) { set.add(t) }
        }
      }
    }
    changes[f.name] = set
  }
  let mut grew = true
  while grew {
    grew = false
    for f in program.functions {
      guard changes.get(f.name) is Some(set) else { continue }
      let before = set.length()
      for g in callees_of(f) {
        match changes.get(g) {
          Some(other) => for t in other { set.add(t) }
          None => for t in tracked { set.add(t) }
        }
      }
      if set.length() != before { grew = true }
    }
  }
  // routines entered only from routines with locals, through known calls
  let entered_otherwise : Set[String] = Set([])
  for f in program.functions {
    if !conv.has_locals(f.name) || f.helper || f.native {
      for g in callees_of(f) { entered_otherwise.add(g) }
      entered_otherwise.add(f.name)
    }
  }
  let candidates = fn(f : @ir.Function) {
    if conv.has_locals(f.name) && !f.helper && !f.native { conv.sig(f.name).outputs & local_vars } else { 0 }
  }
  let exit_candidates = fn() -> Array[(Int, Int)] {
    let out = []
    for r in [1, 2] {
      for a in tracked { out.push((r, a)) }
    }
    out
  }
  let equal_candidates = fn(f : @ir.Function) -> Array[(Int, Int)] {
    if entered_otherwise.contains(f.name) || !conv.has_locals(f.name) { return [] }
    let out = []
    for r in [1, 2] {
      for a in tracked { out.push((r, a)) }
    }
    out
  }
  let facts : Facts = {
    conv,
    writes,
    tracked,
    changes,
    kept: Map(program.functions.map(fn(f) { (f.name, candidates(f)) })),
    equal: Map(program.functions.map(fn(f) { (f.name, equal_candidates(f)) })),
    exits: Map(program.functions.map(fn(f) {
      (f.name, if conv.has_locals(f.name) && !f.helper && !f.native { exit_candidates() } else { [] })
    })),
  }
  // A worklist: a routine is followed again when what it is entered with
  // or what a routine it calls gives back changes; each only narrows.
  let callers : Map[String, Array[String]] = Map([])
  for f in program.functions {
    if !conv.has_locals(f.name) { continue }
    for g in callees_of(f) {
      match callers.get(g) {
        Some(list) => if !list.contains(f.name) { list.push(f.name) }
        None => callers[g] = [f.name]
      }
    }
  }
  // a routine no call with locals reaches keeps no assumption
  for f in program.functions {
    if callers.get(f.name) is None { facts.equal[f.name] = [] }
  }
  // what holds at the calls each routine makes, by callee
  let sites : Map[String, Map[String, Array[(Int, Int)]]] = Map([])
  let queue : Array[String] = program.functions.filter(fn(f) { conv.has_locals(f.name) && !f.native }).map(fn(f) { f.name })
  let queued : Set[String] = Set::from_array(queue)
  let push = fn(g : String) {
    if !queued.contains(g) && conv.has_locals(g) && !program.functions[program.by_name[g]].native {
      queued.add(g)
      queue.push(g)
    }
  }
  let mut next = 0
  while next < queue.length() {
    let name = queue[next]
    next += 1
    queued.remove(name)
    let f = program.functions[program.by_name[name]]
    let (starts, ends) = facts.flow(f)
    let mine : Map[String, Array[(Int, Int)]] = Map([])
    let narrow = fn(g : String, h : Held) {
      let current = mine.get(g).unwrap_or(facts.equal.get(g).unwrap_or([]))
      mine[g] = current.filter(fn(p) { h.same(p.0, byte_base + p.1) })
    }
    for b, block in f.blocks {
      guard starts[b] is Some(start) else { continue }
      let mut h = start.copy()
      for s in block.stmts {
        if s.callee() is Some(g) { narrow(g, h) }
        h = facts.step(h, s)
      }
      match block.term {
        // the cases run after the dispatcher
        Dispatch(helper, _, cases) => {
          narrow(helper, h)
          let after = facts.step(h.copy(), Call(helper))
          for c in cases { narrow(c.1, after) }
        }
        t => for g in t.tail_calls() { narrow(g, h) }
      }
    }
    sites[name] = mine
    // what its callees are entered with
    for g, _ in mine {
      let old = facts.equal.get(g).unwrap_or([])
      if old.is_empty() { continue }
      let mut now = old
      for c in callers.get(g).unwrap_or([]) {
        match sites.get(c) {
          Some(at) => now = now.filter(fn(p) { at.get(g).unwrap_or(now).contains(p) })
          None => ()
        }
      }
      if now.length() != old.length() {
        facts.equal[g] = now
        push(g)
      }
    }
    let mut grew = false
    // what every return leaves equal
    let old_exits = facts.exits[name]
    if !old_exits.is_empty() {
      let mut now = old_exits
      for b, block in f.blocks {
        guard ends[b] is Some(h) else { continue }
        let at = match block.term {
          Return | ReturnValue(_) => Some(h)
          TailCall(g) => Some(facts.step(h.copy(), Call(g)))
          _ => None
        }
        match at {
          Some(e) => now = now.filter(fn(p) { e.same(p.0, byte_base + p.1) })
          None =>
            // a jump or dispatch to other routines: none assumed
            if block.term is (Jump(_, _) | Dispatch(_, _, _)) { now = [] }
        }
      }
      if now.length() != old_exits.length() {
        facts.exits[name] = now
        grew = true
      }
    }
    // what every return gives back as it was
    let old = facts.kept[name]
    if old != 0 {
      let mut keeps = old
      for b, block in f.blocks {
        guard ends[b] is Some(h) else { continue }
        match block.term {
          Return | ReturnValue(_) => {
            let mut bits = 0
            for v in held_vars {
              if h.same(held_index(v), entry_base + held_index(v)) { bits = bits | v.bit() }
            }
            keeps = keeps & bits
          }
          TailCall(g) => keeps = keeps & facts.gives(h, g)
          Jump(_, cases) => for c in cases { keeps = keeps & facts.gives(h, c.1) }
          Dispatch(helper, _, cases) => {
            let after = facts.step(h.copy(), Call(helper))
            for c in cases { keeps = keeps & facts.gives(after, c.1) }
          }
          _ => ()
        }
      }
      if keeps != old {
        facts.kept[name] = keeps
        grew = true
      }
    }
    if grew { for c in callers.get(name).unwrap_or([]) { push(c) } }
  }
  // The calls after which X holds what it held before: it was equal to a
  // location the call leaves alone, and still is (by block << 16 | stmt).
  let same_x : Map[String, Set[Int]] = Map([])
  for f in program.functions {
    if !conv.has_locals(f.name) || f.native { continue }
    let (starts, _) = facts.flow(f)
    let sites : Set[Int] = Set([])
    for b, block in f.blocks {
      guard starts[b] is Some(start) else { continue }
      let mut h = start.copy()
      for i, s in block.stmts {
        let before = h.copy()
        h = facts.step(h, s)
        guard s is Call(g) else { continue }
        let touched = facts.changes.get(g)
        let r = before.rep(1)
        let kept = before.class.iter().any(fn(e) {
          let (loc, cls) = e
          if loc == 1 || cls != r { return false }
          let untouched = if loc >= byte_base && loc < stack_base {
            match touched {
              Some(set) => !set.contains(loc - byte_base)
              None => false
            }
          } else {
            // registers, flags and scratch bytes the call changes are
            // separated by it; stack slots and constants stay
            true
          }
          untouched && h.same(1, loc)
        })
        if kept { sites.add((b << 16) | i) }
      }
    }
    same_x[f.name] = sites
  }
  (facts.kept, facts.equal, same_x)
}
