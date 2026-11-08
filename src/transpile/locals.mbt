// Registers as C locals and parameters (`--locals`).
//
// By default the generated C shares the machine's registers and flags
// between routines as globals, as the 6502 does. With `--locals`, each
// routine has its own: what it reads of its caller's registers and flags are
// its parameters, what its callers read of them after it returns are its
// results, and the rest are locals.
//
//   // in: x; out: a                       uint8_t lakitu_chase(uint8_t x) {
//   void lakitu_chase(void) {        ->       uint8_t a, y;
//     ...                                     ...
//
// A routine's results are its interface's outputs (src/ir/analysis.mbt:
// registers and flags it may change that a caller reads afterwards). The
// first is returned, the others are written through pointers:
//
//   x = queue_metatile_tiles(x, y, &carry);
//
// Its parameters are what is live on its entry when its returns read its
// results: what it reads before writing, and the results it passes through
// unchanged on some path. They are found together for all routines, as a
// fixpoint over the calls (a call reads its callee's parameters and sets its
// results).
//
// Some routines keep the machine's registers, as globals: the hand-written
// routines (`--native`, which share their C with the WebAssembly build), the
// routines they call, the roots, and routines that wait for an interrupt.
// They are printed as without `--locals`; a call from a routine with locals
// to one of them goes through the machine's registers (`machine`), and a
// call from one of them to a routine with locals passes the globals.

// The registers and flags that become locals. SP stays global (the stack
// is memory), and so do the interrupt and decimal flags.
let local_vars : Int = 247 // A, X, Y, C, Z, N, V

priv struct Signature {
  // Keeps the machine's registers (globals).
  machine : Bool
  // Live on entry: what the routine reads of its caller's registers and
  // flags, and the results it may pass through unchanged.
  params : Int
  // What its callers read after it returns.
  outputs : Int
  // The result returned; the others are written through pointers.
  primary : @ir.Var?
}

fn Signature::pointer_bits(self : Signature) -> Int {
  match self.primary {
    Some(p) => self.outputs & p.bit().lnot()
    None => self.outputs
  }
}

// Parameters passed by value: those not written back through a pointer.
fn Signature::value_bits(self : Signature) -> Int {
  self.params & self.pointer_bits().lnot()
}

priv struct Convention {
  locals : Bool
  sigs : Map[String, Signature]
  root : Int // what the idle loop, interrupted, reads: the roots' demand
}

fn Convention::machine() -> Convention {
  { locals: false, sigs: Map([]), root: 0 }
}

fn Convention::sig(self : Convention, name : String) -> Signature {
  match self.sigs.get(name) {
    Some(s) => s
    None => { machine: true, params: local_vars, outputs: local_vars, primary: None }
  }
}

// Whether `name` has locals in this program.
fn Convention::has_locals(self : Convention, name : String) -> Bool {
  self.locals && !self.sig(name).machine
}

// Names of the registers and flags as locals: the interface's.
fn local_name(v : @ir.Var) -> String {
  v.interface_name()
}

fn c_type(v : @ir.Var) -> String {
  if v.is_flag() { "bool" } else { "uint8_t" }
}

// The parameter that writes a result back: `out_y`, or `inout_y` when the
// routine also reads it.
fn pointer_name(sig : Signature, v : @ir.Var) -> String {
  if (sig.params & v.bit()) != 0 { "inout_" + local_name(v) } else { "out_" + local_name(v) }
}

// What is live before a terminator, with registers as locals: returns read
// the routine's results, a tail call its callee's parameters (and the
// results it leaves to the routine).
fn Convention::term_live(self : Convention, f : @ir.Function, term : @ir.Term, live : Array[Int], root : Int) -> Int {
  let own = self.sig(f.name)
  let tail = fn(g : String) {
    let s = self.sig(g)
    s.params | (own.outputs & s.outputs.lnot())
  }
  match term {
    Goto(t) => live[t]
    Branch(c, t, e) => c.vars() | live[t] | live[e]
    Switch(e, _, _) => term.successors().fold(init=e.vars(), fn(acc, t) { acc | live[t] })
    Return => own.outputs
    ReturnValue(e) => own.outputs | e.vars()
    TailCall(g) => tail(g)
    Jump(e, cases) => cases.fold(init=e.vars(), fn(acc, c) { acc | tail(c.1) })
    Dispatch(h, _, cases) => {
      let helper = self.sig(h)
      let after = cases.fold(init=0, fn(acc, c) { acc | tail(c.1) })
      helper.params | (after & helper.outputs.lnot())
    }
    Suspend(t) => root | live[t]
    Yield(_) => root
    Trap(_) => 0
  }
}

// An assignment of a value with no side effects to a local that nothing
// reads: dropped, and what it reads is not live for it. (Without locals the
// value was a global's, which a routine called later could read.)
fn dead(s : @ir.Stmt, after : Int) -> Bool {
  match s {
    Assign(v, e) => (local_vars & v.bit()) != 0 && (after & v.bit()) == 0 && e.is_pure()
    _ => false
  }
}

// What is live before a statement: a call reads its callee's parameters and
// sets its results.
fn Convention::stmt_live(self : Convention, s : @ir.Stmt, after : Int, locals~ : Bool) -> Int {
  match s {
    Call(g) | CallValue(_, g, _) => {
      let sig = self.sig(g)
      sig.params | (after & sig.outputs.lnot())
    }
    _ if locals && dead(s, after) => after
    _ => (after & s.defs().lnot()) | s.uses()
  }
}

// What is live on each block's entry.
fn block_live(f : @ir.Function, conv : Convention, root : Int) -> Array[Int] {
  let locals = conv.has_locals(f.name)
  let live = f.blocks.map(fn(_) { 0 })
  let mut changed = true
  while changed {
    changed = false
    let mut b = f.blocks.length() - 1
    while b >= 0 {
      let block = f.blocks[b]
      let mut l = conv.term_live(f, block.term, live, root)
      let mut k = block.stmts.length() - 1
      while k >= 0 {
        l = conv.stmt_live(block.stmts[k], l, locals~)
        k -= 1
      }
      l = l & local_vars
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
  let locals = self.has_locals(f.name)
  let live = block_live(f, self, self.root)
  f.blocks.map(fn(block) {
    let after = block.stmts.map(fn(_) { 0 })
    let mut l = self.term_live(f, block.term, live, self.root) & local_vars
    let mut k = block.stmts.length() - 1
    while k >= 0 {
      after[k] = l
      l = self.stmt_live(block.stmts[k], l, locals~) & local_vars
      k -= 1
    }
    after
  })
}

// The routines a hand-written routine's original code calls: they keep the
// machine's registers, which the hand-written C reads and writes.
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

fn waits(f : @ir.Function) -> Bool {
  !f.polls.is_empty() ||
  f.blocks.iter().any(fn(block) { block.term is (Yield(_) | Suspend(_)) })
}

// The convention of each routine of `program` (analysed) with `--locals`.
fn Convention::locals_of(program : @ir.Program) -> Convention raise TranspileError {
  if !program.threads.is_empty() { raise LocalsUnsupported("coroutines (the map's threads)") }
  if !program.restarts.is_empty() { raise LocalsUnsupported("restarts") }
  let info = program.info.unwrap()
  let callees = native_callees(program)
  let sigs : Map[String, Signature] = Map([])
  for f in program.functions {
    let machine = f.native || program.is_root(f.name) || callees.contains(f.name) || waits(f)
    let outputs = program.outputs(info, f.name) & local_vars
    let primary = if f.helper { None } else { @ir.vars_of(outputs).get(0) }
    if f.helper && machine { raise LocalsUnsupported("a dispatcher called by hand-written code: \{f.name}") }
    sigs[f.name] = { machine, params: 0, outputs, primary }
  }
  let conv = { locals: true, sigs, root: info.root_demand & local_vars }
  // least fixpoint: parameters only grow
  let mut changed = true
  while changed {
    changed = false
    for f in program.functions {
      let entry = block_live(f, conv, conv.root)[f.entry]
      let old = sigs[f.name]
      if (old.params | entry) != old.params {
        sigs[f.name] = { ..old, params: old.params | entry }
        changed = true
      }
    }
  }
  conv
}

// The parameters of a routine with locals, for its prototype.
fn Convention::parameters(self : Convention, f : @ir.Function) -> String {
  let sig = self.sig(f.name)
  let list = []
  if f.helper { list.push("uint16_t return_address") }
  for v in @ir.vars_of(sig.value_bits()) { list.push("\{c_type(v)} \{local_name(v)}") }
  for v in @ir.vars_of(sig.pointer_bits()) { list.push("\{c_type(v)} *\{pointer_name(sig, v)}") }
  if list.is_empty() { "void" } else { list.join(", ") }
}

fn Convention::return_type(self : Convention, f : @ir.Function) -> String {
  if f.helper { return "uint16_t" }
  match self.sig(f.name).primary {
    Some(v) => c_type(v)
    None => "void"
  }
}

fn Convention::prototype(self : Convention, f : @ir.Function) -> String {
  if !self.has_locals(f.name) { return "void \{f.name}(void)" }
  "\{self.return_type(f)} \{f.name}(\{self.parameters(f)})"
}
