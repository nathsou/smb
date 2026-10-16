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
    Yield(_) => self.root_demand | SP_BIT
    Trap(_) => 0
  }
}

pub fn Analysis::stmt_live(self : Analysis, s : Stmt, after : Int) -> Int {
  match s {
    Call(g) => self.get_in(g)
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

fn Program::liveness(self : Program, info : Analysis) -> Unit {
  // Least fixpoint: known routines start with nothing live.
  for f in self.functions {
    info.live_in[f.name] = 0
    info.demand[f.name] = 0
  }
  let mut changed = true
  while changed {
    changed = false
    for f in self.functions {
      let block_in = info.blocks_live(f)
      if add(info.live_in, f.name, block_in[f.entry]) { changed = true }
      for block in f.blocks {
        let mut live = info.term_live(f, block.term, block_in)
        let d = info.demand.get(f.name).unwrap_or(0)
        match block.term {
          TailCall(g) => if add(info.demand, g, d) { changed = true }
          Dispatch(h, _, cases) => {
            let mut after = 0
            for c in cases {
              after = after | info.get_in(c.1)
              if add(info.demand, c.1, d) { changed = true }
            }
            if add(info.demand, h, after) { changed = true }
          }
          _ => ()
        }
        let mut k = block.stmts.length() - 1
        while k >= 0 {
          if block.stmts[k] is Call(g) && add(info.demand, g, live) { changed = true }
          live = info.stmt_live(block.stmts[k], live)
          k -= 1
        }
      }
    }
    let mut roots = 0
    for r in self.roots() { roots = roots | info.get_in(r) }
    if roots != info.root_demand {
      info.root_demand = roots
      changed = true
    }
    for r in self.roots() {
      if add(info.demand, r, info.root_demand) { changed = true }
    }
  }
}

fn Program::summaries(self : Program, info : Analysis) -> Unit {
  // May-write: everything assigned, with callees and tail targets.
  for f in self.functions { info.may_write[f.name] = 0 }
  let mut changed = true
  while changed {
    changed = false
    for f in self.functions {
      let mut w = 0
      for block in f.blocks {
        for s in block.stmts {
          w = w | s.defs()
          if s is Call(g) { w = w | info.may_write.get(g).unwrap_or(everything) }
        }
        match block.term {
          TailCall(g) => w = w | info.may_write.get(g).unwrap_or(everything)
          Dispatch(h, _, cases) => {
            w = w | info.may_write.get(h).unwrap_or(everything)
            for c in cases { w = w | info.may_write.get(c.1).unwrap_or(everything) }
          }
          _ => ()
        }
      }
      if add(info.may_write, f.name, w) { changed = true }
    }
  }
  // Must-write: written on every path to every return (greatest fixpoint).
  for f in self.functions { info.must_write[f.name] = everything }
  changed = true
  while changed {
    changed = false
    for f in self.functions {
      let preds : Array[Array[Int]] = f.blocks.map(fn(_) { [] })
      for b, block in f.blocks {
        for s in block.term.successors() { preds[s].push(b) }
      }
      let out = f.blocks.map(fn(_) { everything })
      let mut inner = true
      while inner {
        inner = false
        for b, block in f.blocks {
          let mut w = if b == f.entry {
            0
          } else {
            preds[b].fold(init=everything, fn(acc, p) { acc & out[p] })
          }
          for s in block.stmts {
            w = w | s.defs()
            if s is Call(g) { w = w | info.must_write.get(g).unwrap_or(0) }
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
          Dispatch(h, _, cases) => {
            let mut cases_must = everything
            for c in cases { cases_must = cases_must & info.must_write.get(c.1).unwrap_or(0) }
            must = must & (out[b] | info.must_write.get(h).unwrap_or(0) | cases_must)
          }
          _ => ()
        }
      }
      if must != info.must_write[f.name] {
        info.must_write[f.name] = must
        changed = true
      }
    }
  }
  // Reads: live on entry when nothing is demanded after the routine.
  for f in self.functions { info.reads[f.name] = 0 }
  changed = true
  while changed {
    changed = false
    for f in self.functions {
      let block_in = f.blocks.map(fn(_) { 0 })
      let mut inner = true
      while inner {
        inner = false
        let mut b = f.blocks.length() - 1
        while b >= 0 {
          let block = f.blocks[b]
          let mut l = match block.term {
            Goto(t) => block_in[t]
            Branch(c, t, e) => c.vars() | block_in[t] | block_in[e]
            ReturnValue(e) => e.vars()
            TailCall(g) => info.reads.get(g).unwrap_or(everything)
            Dispatch(h, _, cases) => {
              let mut after = 0
              for c in cases { after = after | info.reads.get(c.1).unwrap_or(everything) }
              info.reads.get(h).unwrap_or(everything) |
              (after & info.must_write.get(h).unwrap_or(0).lnot())
            }
            _ => 0
          }
          let mut k = block.stmts.length() - 1
          while k >= 0 {
            l = match block.stmts[k] {
              Call(g) =>
                info.reads.get(g).unwrap_or(everything) |
                (l & info.must_write.get(g).unwrap_or(0).lnot())
              s => (l & s.defs().lnot()) | s.uses()
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
      if add(info.reads, f.name, block_in[f.entry]) { changed = true }
    }
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
