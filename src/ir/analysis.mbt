// Interprocedural liveness and routine interfaces over the IR.
//
// Matched call/return demands, as in the lowering's analysis of the machine
// code: a routine's returns demand what its callers read after the call
// (`demand`), and a call reads what the callee reads on entry (`live_in`),
// which already includes the caller's demand flowing through the callee.
// Roots return to the idle loop or to the interrupted idle loop, which read
// nothing; there the demand is what the roots themselves read on entry.
// SP is always live at returns: callers rely on a balanced stack.

pub(all) struct Analysis {
  live_in : Map[String, Int]
  demand : Map[String, Int]
  mut root_demand : Int
  // Interfaces: registers read before written, and what may or must change.
  reads : Map[String, Int]
  may_write : Map[String, Int]
  must_write : Map[String, Int]
} derive(Debug)

let everything : Int = 1023

fn Analysis::get_in(self : Analysis, name : String) -> Int {
  self.live_in.get(name).unwrap_or(everything)
}

// Live variables before a terminator.
pub fn Analysis::term_live(self : Analysis, f : Function, term : Term, block_in : Array[Int]) -> Int {
  match term {
    Goto(b) => block_in[b]
    Branch(c, t, e) => c.vars() | block_in[t] | block_in[e]
    Return => self.demand.get(f.name).unwrap_or(0) | SP_BIT
    ReturnValue(e) => self.demand.get(f.name).unwrap_or(0) | SP_BIT | e.vars()
    TailCall(g) => self.get_in(g)
    Dispatch(h, _, _) => self.get_in(h)
    Jump(e, cases) => cases.fold(init=e.vars(), fn(acc, c) { acc | self.get_in(c.1) })
    Switch(e, _, _) => term.successors().fold(init=e.vars(), fn(acc, b) { acc | block_in[b] })
    Suspend(b) => self.root_demand | SP_BIT | block_in[b]
    Yield(_) => self.root_demand | SP_BIT
    Trap(_) => 0
  }
}

pub fn Analysis::stmt_live(self : Analysis, s : Stmt, after : Int) -> Int {
  match s {
    Call(g) | CallValue(_, g, _) => self.get_in(g)
    _ => (after & s.defs().lnot()) | s.uses()
  }
}

// Live-in of every block of `f`.
pub fn Analysis::blocks_live(self : Analysis, f : Function) -> Array[Int] {
  let live = f.blocks.map(fn(_) { 0 })
  let mut changed = true
  while changed {
    changed = false
    let mut b = f.blocks.length() - 1
    while b >= 0 {
      let block = f.blocks[b]
      let mut l = self.term_live(f, block.term, live)
      let mut k = block.stmts.length() - 1
      while k >= 0 {
        l = self.stmt_live(block.stmts[k], l)
        k -= 1
      }
      if l != live[b] {
        live[b] = l
        changed = true
      }
      b -= 1
    }
  }
  live
}

// Variables live after each block's last statement (before its terminator
// is the same set plus the terminator's reads).
pub fn Analysis::blocks_live_out(self : Analysis, f : Function, block_in : Array[Int]) -> Array[Int] {
  f.blocks.map(fn(block) {
    match block.term {
      Goto(b) => block_in[b]
      Branch(_, t, e) => block_in[t] | block_in[e]
      Switch(_, _, _) as other => other.successors().fold(init=0, fn(acc, b) { acc | block_in[b] })
      other => self.term_live(f, other, block_in)
    }
  })
}

fn add(map : Map[String, Int], name : String, bits : Int) -> Bool {
  let old = map.get(name).unwrap_or(0)
  if (old | bits) != old {
    map[name] = old | bits
    true
  } else {
    false
  }
}

// A block's statements for the fixpoints below, computed once: runs of
// statements without calls as one step (what the run writes, and what it
// reads before writing), and the calls.
priv enum Step {
  Run(Int, Int) // kill, gen: live before = (live after & ~kill) | gen
  Calls(String)
}

priv struct Shape {
  steps : Array[Array[Step]] // by block, in order
  term_vars : Array[Int] // what each block's terminator reads itself
  preds : Array[Array[Int]]
  callees : Array[String] // routines it calls, tail calls or jumps to
  uses_root : Bool // suspends or yields: reads the roots' demand
}

fn shape_of(f : Function) -> Shape {
  let callees = []
  let note = fn(g : String) { if !callees.contains(g) { callees.push(g) } }
  let preds : Array[Array[Int]] = f.blocks.map(fn(_) { [] })
  let mut uses_root = false
  let steps = f.blocks.mapi(fn(b, block) {
    for t in block.term.successors() { preds[t].push(b) }
    let out : Array[Step] = []
    let mut kill = 0
    let mut gen = 0
    let mut open = false
    for s in block.stmts {
      match s {
        Call(g) | CallValue(_, g, _) => {
          if open { out.push(Run(kill, gen)) }
          kill = 0
          gen = 0
          open = false
          out.push(Calls(g))
          note(g)
        }
        _ => {
          // the run so far, then s
          gen = gen | (s.uses() & kill.lnot())
          kill = kill | s.defs()
          open = true
        }
      }
    }
    if open { out.push(Run(kill, gen)) }
    match block.term {
      TailCall(g) => note(g)
      Jump(_, cases) => for c in cases { note(c.1) }
      Dispatch(h, _, cases) => {
        note(h)
        for c in cases { note(c.1) }
      }
      Suspend(_) | Yield(_) => uses_root = true
      _ => ()
    }
    out
  })
  let term_vars = f.blocks.map(fn(block) {
    match block.term {
      Branch(c, _, _) => c.vars()
      ReturnValue(e) | Jump(e, _) | Switch(e, _, _) => e.vars()
      _ => 0
    }
  })
  { steps, term_vars, preds, callees, uses_root }
}

// The functions and their shapes, and who calls each routine.
priv struct Graph {
  functions : Map[String, Function]
  shapes : Map[String, Shape]
  callers : Map[String, Array[String]]
}

fn Program::graph(self : Program) -> Graph {
  let functions = Map([])
  let shapes = Map([])
  let callers : Map[String, Array[String]] = Map([])
  for f in self.functions {
    functions[f.name] = f
    let shape = shape_of(f)
    shapes[f.name] = shape
    for g in shape.callees {
      match callers.get(g) {
        Some(list) => list.push(f.name)
        None => callers[g] = [f.name]
      }
    }
  }
  { functions, shapes, callers }
}

// A queue of routines to (re)analyse, each in it once.
priv struct Work {
  queue : Array[String]
  queued : Set[String]
  mut next : Int
}

fn Work::new(names : Array[String]) -> Work {
  { queue: names.copy(), queued: Set(names), next: 0 }
}

fn Work::push(self : Work, name : String) -> Unit {
  if !self.queued.contains(name) {
    self.queued.add(name)
    self.queue.push(name)
  }
}

fn Work::pop(self : Work) -> String? {
  if self.next >= self.queue.length() {
    self.queue.clear()
    self.next = 0
    return None
  }
  let name = self.queue[self.next]
  self.next += 1
  self.queued.remove(name)
  Some(name)
}

fn Work::push_all(self : Work, names : Array[String]?) -> Unit {
  match names {
    Some(list) => for n in list { self.push(n) }
    None => ()
  }
}

fn Program::liveness(self : Program, info : Analysis) -> Unit {
  // Least fixpoint: known routines start with nothing live.
  let graph = self.graph()
  let names = self.functions.map(fn(f) { f.name })
  for name in names {
    info.live_in[name] = 0
    info.demand[name] = 0
  }
  let roots = self.roots()
  let root_users = self.functions.filter(fn(f) { graph.shapes[f.name].uses_root }).map(fn(f) { f.name })
  let work = Work::new(names)
  let demand = fn(g : String, bits : Int) { if add(info.demand, g, bits) { work.push(g) } }
  while work.pop() is Some(name) {
    let f = graph.functions[name]
    let shape = graph.shapes[name]
    let d = info.demand.get(name).unwrap_or(0)
    // the block's live-in from the variables live before its terminator
    let through = fn(b : Int, live : Int, record : Bool) {
      let steps = shape.steps[b]
      let mut l = live
      let mut k = steps.length() - 1
      while k >= 0 {
        match steps[k] {
          Run(kill, gen) => l = (l & kill.lnot()) | gen
          Calls(g) => {
            if record { demand(g, l) }
            l = info.get_in(g)
          }
        }
        k -= 1
      }
      l
    }
    let before_term = fn(b : Int, block_in : Array[Int]) {
      let tv = shape.term_vars[b]
      match f.blocks[b].term {
        Goto(t) => block_in[t]
        Branch(_, t, e) => tv | block_in[t] | block_in[e]
        Return => d | SP_BIT
        ReturnValue(_) => d | SP_BIT | tv
        TailCall(g) => info.get_in(g)
        Dispatch(h, _, _) => info.get_in(h)
        Jump(_, cases) => cases.fold(init=tv, fn(acc, c) { acc | info.get_in(c.1) })
        Switch(_, _, _) as term => term.successors().fold(init=tv, fn(acc, t) { acc | block_in[t] })
        Suspend(t) => info.root_demand | SP_BIT | block_in[t]
        Yield(_) => info.root_demand | SP_BIT
        Trap(_) => 0
      }
    }
    let block_in = f.blocks.map(fn(_) { 0 })
    let mut changed = true
    while changed {
      changed = false
      let mut b = f.blocks.length() - 1
      while b >= 0 {
        let l = through(b, before_term(b, block_in), false)
        if l != block_in[b] {
          block_in[b] = l
          changed = true
        }
        b -= 1
      }
    }
    // what the routine demands of the routines it calls
    for b, block in f.blocks {
      match block.term {
        TailCall(g) => demand(g, d)
        Jump(_, cases) => for c in cases { demand(c.1, d) }
        Dispatch(h, _, cases) => {
          let mut after = 0
          for c in cases {
            after = after | info.get_in(c.1)
            demand(c.1, d)
          }
          demand(h, after)
        }
        _ => ()
      }
      through(b, before_term(b, block_in), true) |> ignore
    }
    if add(info.live_in, name, block_in[f.entry]) {
      work.push_all(graph.callers.get(name))
      if roots.contains(name) {
        let mut r = 0
        for root in roots { r = r | info.get_in(root) }
        if r != info.root_demand {
          info.root_demand = r
          for user in root_users { work.push(user) }
        }
        for root in roots { demand(root, info.root_demand) }
      }
    }
  }
  // the roots read what they demand of each other
  for root in roots { add(info.demand, root, info.root_demand) |> ignore }
}

fn Program::summaries(self : Program, info : Analysis) -> Unit {
  let graph = self.graph()
  let names = self.functions.map(fn(f) { f.name })
  // Each a fixpoint over the routines: a routine is analysed again when
  // what it reads of a callee changes.
  // May-write: everything assigned, with callees and tail targets.
  for name in names { info.may_write[name] = 0 }
  let work = Work::new(names)
  while work.pop() is Some(name) {
    let shape = graph.shapes[name]
    let mut w = 0
    for steps in shape.steps {
      for step in steps {
        if step is Run(kill, _) { w = w | kill }
      }
    }
    for g in shape.callees { w = w | info.may_write.get(g).unwrap_or(everything) }
    if add(info.may_write, name, w) { work.push_all(graph.callers.get(name)) }
  }
  // Must-write: written on every path to every return (greatest fixpoint).
  for name in names { info.must_write[name] = everything }
  let work = Work::new(names)
  while work.pop() is Some(name) {
    let f = graph.functions[name]
    let shape = graph.shapes[name]
    let out = f.blocks.map(fn(_) { everything })
    let mut inner = true
    while inner {
      inner = false
      for b in 0..<f.blocks.length() {
        let mut w = if b == f.entry {
          0
        } else {
          shape.preds[b].fold(init=everything, fn(acc, p) { acc & out[p] })
        }
        for step in shape.steps[b] {
          match step {
            Run(kill, _) => w = w | kill
            Calls(g) => w = w | info.must_write.get(g).unwrap_or(0)
          }
        }
        if w != out[b] {
          out[b] = w
          inner = true
        }
      }
    }
    let mut must = everything
    for b, block in f.blocks {
      match block.term {
        Return | ReturnValue(_) => must = must & out[b]
        TailCall(g) => must = must & (out[b] | info.must_write.get(g).unwrap_or(0))
        Jump(_, cases) => {
          let mut cases_must = everything
          for c in cases { cases_must = cases_must & info.must_write.get(c.1).unwrap_or(0) }
          must = must & (out[b] | cases_must)
        }
        Dispatch(h, _, cases) => {
          let mut cases_must = everything
          for c in cases { cases_must = cases_must & info.must_write.get(c.1).unwrap_or(0) }
          must = must & (out[b] | info.must_write.get(h).unwrap_or(0) | cases_must)
        }
        _ => ()
      }
    }
    if must != info.must_write[name] {
      info.must_write[name] = must
      work.push_all(graph.callers.get(name))
    }
  }
  // Reads: live on entry when nothing is demanded after the routine.
  for name in names { info.reads[name] = 0 }
  let work = Work::new(names)
  while work.pop() is Some(name) {
    let f = graph.functions[name]
    let shape = graph.shapes[name]
    let reads = fn(g : String) { info.reads.get(g).unwrap_or(everything) }
    let block_in = f.blocks.map(fn(_) { 0 })
    let mut inner = true
    while inner {
      inner = false
      let mut b = f.blocks.length() - 1
      while b >= 0 {
        let tv = shape.term_vars[b]
        let mut l = match f.blocks[b].term {
          Goto(t) => block_in[t]
          Branch(_, t, e) => tv | block_in[t] | block_in[e]
          ReturnValue(_) => tv
          TailCall(g) => reads(g)
          Jump(_, cases) => cases.fold(init=tv, fn(acc, c) { acc | reads(c.1) })
          Switch(_, _, _) as term => term.successors().fold(init=tv, fn(acc, t) { acc | block_in[t] })
          Suspend(t) => block_in[t]
          Dispatch(h, _, cases) => {
            let mut after = 0
            for c in cases { after = after | reads(c.1) }
            reads(h) | (after & info.must_write.get(h).unwrap_or(0).lnot())
          }
          _ => 0
        }
        let steps = shape.steps[b]
        let mut k = steps.length() - 1
        while k >= 0 {
          l = match steps[k] {
            Calls(g) => reads(g) | (l & info.must_write.get(g).unwrap_or(0).lnot())
            Run(kill, gen) => (l & kill.lnot()) | gen
          }
          k -= 1
        }
        if l != block_in[b] {
          block_in[b] = l
          inner = true
        }
        b -= 1
      }
    }
    if add(info.reads, name, block_in[f.entry]) { work.push_all(graph.callers.get(name)) }
  }
}

pub fn Program::analyze(self : Program) -> Analysis {
  let info : Analysis = {
    live_in: Map([]),
    demand: Map([]),
    root_demand: 0,
    reads: Map([]),
    may_write: Map([]),
    must_write: Map([]),
  }
  self.liveness(info)
  self.summaries(info)
  self.info = Some(info)
  info
}

pub fn Program::is_root(self : Program, name : String) -> Bool {
  self.roots().contains(name)
}

// Registers a routine reads before writing them.
pub fn Analysis::inputs(self : Analysis, name : String) -> Int {
  self.reads.get(name).unwrap_or(everything) & REGISTERS
}

// Registers and status flags a routine may change that a caller reads.
pub fn Program::outputs(self : Program, info : Analysis, name : String) -> Int {
  if self.is_root(name) { return 0 }
  info.may_write.get(name).unwrap_or(everything) &
  info.demand.get(name).unwrap_or(everything) &
  (REGISTERS | STATUS_FLAGS)
}
