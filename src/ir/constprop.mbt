// Constant propagation, across blocks and calls.
//
// A forward dataflow over each function: every machine variable is unknown
// at entry, assignments of constant expressions make it known, a call makes
// what the callee may write unknown, and paths merge to the common value.
// Uses of known variables become constants, reads of PRG ROM at known
// addresses become the byte read, and branches on known conditions become
// jumps (`prune` then drops the blocks no longer reached).

let unreached : Int = -2
let varying : Int = -1

fn Program::rom_byte(self : Program, address : Int) -> Int? {
  let size = self.prg.length()
  if size == 0 || address < 0x8000 || address > 0xffff { return None }
  Some(self.prg[(address - 0x8000) % size])
}

fn Program::fold(self : Program, e : Expr, known : Array[Int]) -> Expr {
  let e = match e {
    Var(v) => {
      let k = known[all_vars_index(v)]
      if k >= 0 { Const(k) } else { e }
    }
    Load(m, a) => {
      let a = self.fold(a, known)
      match (m, a.value()) {
        (Rom, Some(address)) =>
          match self.rom_byte(address) {
            Some(b) => Const(b)
            None => Load(m, a)
          }
        _ => Load(m, a)
      }
    }
    Unary(op, a) => Unary(op, self.fold(a, known))
    Binary(op, l, r) => Binary(op, self.fold(l, known), self.fold(r, known))
    other => other
  }
  simplify(e)
}

fn all_vars_index(v : Var) -> Int {
  match v {
    A => 0; X => 1; Y => 2; SP => 3
    C => 4; Z => 5; N => 6; V => 7; I => 8; D => 9
  }
}

fn meet(a : Array[Int], b : Array[Int]) -> Array[Int] {
  Array::makei(a.length(), fn(i) {
    if a[i] == unreached {
      b[i]
    } else if b[i] == unreached || a[i] == b[i] {
      a[i]
    } else {
      varying
    }
  })
}

fn Program::transfer(self : Program, info : Analysis, s : Stmt, known : Array[Int], temps : Array[Temp]) -> Unit {
  match s {
    Assign(v, e) => {
      let value = v.width().wrap(self.fold(e, known), temps)
      known[all_vars_index(v)] = match simplify(value).value() {
        Some(c) => c
        None => varying
      }
    }
    Call(g) => {
      let writes = info.may_write.get(g).unwrap_or(everything)
      for i, v in all_vars {
        if (writes & v.bit()) != 0 { known[i] = varying }
      }
    }
    _ => ()
  }
}

fn Program::const_prop(self : Program, info : Analysis, f : Function) -> Bool {
  let n = f.blocks.length()
  let entry_state : Array[Array[Int]] = Array::makei(n, fn(_) { Array::make(10, unreached) })
  entry_state[f.entry] = Array::make(10, varying)
  let mut changed = true
  while changed {
    changed = false
    for b, block in f.blocks {
      if entry_state[b][0] == unreached && b != f.entry {
        // reached blocks have every variable at least `varying`
        continue
      }
      let known = entry_state[b].copy()
      for s in block.stmts { self.transfer(info, s, known, f.temps) }
      let targets = match block.term {
        Branch(c, t, e) =>
          match self.fold(c, known).value() {
            Some(0) => [e]
            Some(_) => [t]
            None => [t, e]
          }
        term => term.successors()
      }
      for t in targets {
        let merged = meet(entry_state[t], known)
        if merged != entry_state[t] {
          entry_state[t] = merged
          changed = true
        }
      }
    }
  }
  // Rewrite with what is known before each statement.
  let mut rewrote = false
  for b, block in f.blocks {
    if entry_state[b][0] == unreached && b != f.entry { continue }
    let known = entry_state[b].copy()
    let stmts = block.stmts.map(fn(s) {
      let folded = match s {
        Call(_) => s
        _ => s.map_exprs(fn(e) { self.fold(e, known) })
      }
      self.transfer(info, s, known, f.temps)
      folded
    })
    if stmts != block.stmts {
      block.stmts = stmts
      rewrote = true
    }
    let term = match block.term.map_exprs(fn(e) { self.fold(e, known) }) {
      Branch(c, t, e) =>
        match c.value() {
          Some(0) => Goto(e)
          Some(_) => Goto(t)
          None => Branch(c, t, e)
        }
      other => other
    }
    if term != block.term {
      block.term = term
      rewrote = true
    }
  }
  rewrote
}

// Drops blocks that the entry no longer reaches.
fn prune(f : Function) -> Unit {
  let reachable = Array::make(f.blocks.length(), false)
  let work = [f.entry]
  while work.pop() is Some(b) {
    if reachable[b] { continue }
    reachable[b] = true
    for s in f.blocks[b].term.successors() { work.push(s) }
  }
  if reachable.iter().all(fn(r) { r }) { return }
  let index = Array::make(f.blocks.length(), -1)
  let kept = []
  for b, block in f.blocks {
    if reachable[b] {
      index[b] = kept.length()
      kept.push(block)
    }
  }
  for block in kept { block.term = block.term.map_targets(fn(t) { index[t] }) }
  f.blocks = kept
  f.entry = index[f.entry]
}

// RAM stores overwritten in the same block before anything could read them.
fn dead_stores(f : Function) -> Bool {
  let mut changed = false
  for block in f.blocks {
    let stmts = block.stmts
    let keep = Array::make(stmts.length(), true)
    for i, s in stmts {
      guard s is Store(Ram, addr, _) && addr.value() is Some(a) else { continue }
      let mut j = i + 1
      while j < stmts.length() {
        let t = stmts[j]
        // anything that may read RAM ends the search
        let reads = t.exprs().iter().any(fn(e) { e.reads_memory() })
        if reads || t is Call(_) || t is Effect(_, _) { break }
        match t {
          Store(Ram, other, _) if other.value() == Some(a) => {
            keep[i] = false
            break
          }
          Store(Bus, _, _) => break
          _ => ()
        }
        j += 1
      }
    }
    if keep.iter().any(fn(k) { !k }) {
      let out = []
      for i, s in stmts {
        if keep[i] { out.push(s) }
      }
      block.stmts = out
      changed = true
    }
  }
  changed
}
