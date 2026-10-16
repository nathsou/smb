// Optimisations on the IR, run to a fixpoint with the analysis:
//
// - dead code: assignments to variables nobody reads (flags in particular),
//   unused temporaries; reads with effects stay as `Eval`;
// - forwarding: a value with one use later in the same block moves into
//   that use (loads stored elsewhere, flags folded into branches);
// - local simplification of every expression;
// - constant propagation and dead stores (constprop.mbt).

// Expressions larger than this are not grown further by forwarding.
let forward_limit : Int = 14

fn Function::temp_uses(self : Function) -> Array[Int] {
  let uses = self.temps.map(fn(_) { 0 })
  let found = []
  for block in self.blocks {
    for s in block.stmts {
      for e in s.exprs() { e.temps(found) }
    }
    for e in block.term.exprs() { e.temps(found) }
  }
  for t in found { uses[t] += 1 }
  uses
}

fn dead_code(info : Analysis, f : Function) -> Bool {
  let mut changed = false
  let block_in = info.blocks_live(f)
  let uses = f.temp_uses()
  for block in f.blocks {
    let mut live = info.term_live(f, block.term, block_in)
    let kept = []
    let mut k = block.stmts.length() - 1
    while k >= 0 {
      let s = block.stmts[k]
      let replacement : Stmt? = match s {
        Assign(v, e) if (live & v.bit()) == 0 =>
          if e.is_pure() { None } else { Some(Eval(e)) }
        Assign(v, Var(u)) if u == v => None
        Let(t, e) if uses[t] == 0 => if e.is_pure() { None } else { Some(Eval(e)) }
        Eval(e) if e.is_pure() => None
        other => Some(other)
      }
      match replacement {
        Some(r) => {
          if r != s { changed = true }
          kept.push(r)
          live = info.stmt_live(r, live)
        }
        None => changed = true
      }
      k -= 1
    }
    kept.rev_in_place()
    block.stmts = kept
  }
  changed
}

priv enum Name {
  NVar(Var)
  NTemp(Int)
}

fn Name::count(self : Name, e : Expr) -> Int {
  match self {
    NVar(v) => e.uses_var(v)
    NTemp(t) => e.uses_temp(t)
  }
}

fn Name::replace(self : Name, e : Expr, by : Expr) -> Expr {
  match self {
    NVar(v) => e.replace_var(v, by)
    NTemp(t) => e.replace_temp(t, by)
  }
}

fn count_in(name : Name, exprs : Array[Expr]) -> Int {
  exprs.fold(init=0, fn(acc, e) { acc + name.count(e) })
}

fn writes_memory(s : Stmt) -> Bool {
  match s {
    Store(_, _, _) | Effect(_, _) | Call(_) => true
    _ => false
  }
}

// Moves single-use definitions into their use within each block.
fn forward(info : Analysis, f : Function) -> Bool {
  let mut changed = false
  let block_in = info.blocks_live(f)
  let live_out = info.blocks_live_out(f, block_in)
  let uses = f.temp_uses()
  for b, block in f.blocks {
    let stmts = block.stmts
    let mut i = 0
    while i < stmts.length() {
      // the value as stored, with the destination's truncation
      let (name, e) = match stmts[i] {
        Assign(v, e) => (NVar(v), v.width().wrap(e, f.temps))
        Let(t, e) => (NTemp(t), f.temps[t].width.wrap(e, f.temps))
        _ => {
          i += 1
          continue
        }
      }
      let pure = e.is_pure()
      // Find the use; stop where the value would change or be lost.
      let mut use_at : Int? = None // stmts.length() means the terminator
      let mut j = i + 1
      let mut blocked = false
      while j <= stmts.length() {
        if j == stmts.length() {
          if count_in(name, block.term.exprs()) > 0 { use_at = Some(j) }
          break
        }
        let s = stmts[j]
        if count_in(name, s.exprs()) > 0 {
          use_at = Some(j)
          break
        }
        if !pure { blocked = true; break }
        match (s, name) {
          (Call(_), NVar(_)) => { blocked = true; break }
          _ => ()
        }
        if (s.defs() & e.vars()) != 0 { blocked = true; break }
        if (s is Call(_)) && (e.vars() != 0 || e.reads_memory()) { blocked = true; break }
        if e.reads_memory() && writes_memory(s) { blocked = true; break }
        if name is NVar(v) && (s.defs() & v.bit()) != 0 { blocked = true; break }
        j += 1
      }
      if blocked || use_at is None {
        i += 1
        continue
      }
      let at = use_at.unwrap()
      if !pure && at != i + 1 {
        i += 1
        continue
      }
      let use_exprs = if at == stmts.length() { block.term.exprs() } else { stmts[at].exprs() }
      let occurrences = count_in(name, use_exprs)
      if occurrences > 1 && !e.is_atom() {
        i += 1
        continue
      }
      // The definition must be dead after this use.
      let dead_after = match name {
        NTemp(t) => uses[t] == occurrences
        NVar(v) => {
          let mut dead = true
          // the use statement may itself redefine the variable
          let mut redefined = at < stmts.length() && (stmts[at].defs() & v.bit()) != 0
          if at < stmts.length() && !redefined {
            let mut k = at + 1
            while k < stmts.length() {
              let s = stmts[k]
              if count_in(name, s.exprs()) > 0 { dead = false; break }
              // a call reads what is live into the callee, and nothing
              // else of the caller's survives it unread
              if s is Call(g) {
                if (info.get_in(g) & v.bit()) != 0 { dead = false } else { redefined = true }
                break
              }
              if (s.defs() & v.bit()) != 0 { redefined = true; break }
              k += 1
            }
            if dead && !redefined && count_in(name, block.term.exprs()) > 0 { dead = false }
          }
          dead && (redefined || (live_out[b] & v.bit()) == 0)
        }
      }
      if !dead_after {
        i += 1
        continue
      }
      let grown = use_exprs.fold(init=0, fn(acc, x) { acc + x.size() }) + e.size() * occurrences
      if grown > forward_limit && !e.is_atom() {
        i += 1
        continue
      }
      if at == stmts.length() {
        block.term = block.term.map_exprs(fn(x) { simplify(name.replace(x, e)) })
      } else {
        stmts[at] = simplify_stmt(stmts[at].map_exprs(fn(x) { name.replace(x, e) }))
      }
      stmts.remove(i) |> ignore
      changed = true
    }
  }
  changed
}

// A value stored in a byte needs no byte truncation of its own, and
// truncations inside sums go with it: (uint8_t)((uint8_t)(x - 1) + 1) is x.
fn stored_byte(e : Expr) -> Expr {
  match simplify(Unary(Byte, e)) {
    Unary(Byte, inner) => inner
    other => other
  }
}

fn simplify_stmt(s : Stmt) -> Stmt {
  match s.map_exprs(simplify) {
    Assign(v, e) if !v.is_flag() => Assign(v, stored_byte(e))
    Store(m, a, e) => Store(m, a, stored_byte(e))
    other => other
  }
}

fn simplify_all(f : Function) -> Unit {
  for block in f.blocks {
    block.stmts = block.stmts.map(simplify_stmt)
    block.term = block.term.map_exprs(simplify)
  }
}

// Branches to empty blocks that only jump go straight to the destination.
fn thread_jumps(f : Function) -> Unit {
  fn resolve(b : Int) -> Int {
    let mut t = b
    let mut steps = 0
    while steps < 16 && f.blocks[t].stmts.is_empty() && (f.blocks[t].term is Goto(_)) {
      match f.blocks[t].term {
        Goto(next) => t = next
        _ => ()
      }
      steps += 1
    }
    t
  }
  for block in f.blocks {
    block.term = block.term.map_targets(resolve)
    match block.term {
      Branch(_, t, e) if t == e => block.term = Goto(t)
      _ => ()
    }
  }
  f.entry = resolve(f.entry)
}

pub fn Program::optimize(self : Program) -> Unit {
  for f in self.functions {
    simplify_all(f)
    thread_jumps(f)
  }
  let mut rounds = 0
  while rounds < 20 {
    let info = self.analyze()
    let mut changed = false
    for f in self.functions {
      if self.const_prop(info, f) {
        changed = true
        prune(f)
        thread_jumps(f)
      }
      if dead_code(info, f) { changed = true }
      if dead_stores(f) { changed = true }
      if forward(info, f) { changed = true }
    }
    if !changed { break }
    rounds += 1
  }
  self.analyze() |> ignore
}

// Inlines small leaf routines: one block of at most `limit` statements,
// no calls, ending in a return. Their temporaries become the caller's.
pub fn Program::inline_leaves(self : Program, limit~ : Int = 12) -> Int {
  let leaves : Map[String, Function] = Map([])
  for f in self.functions {
    if f.native || f.helper || f.blocks.length() != 1 { continue }
    let block = f.blocks[0]
    if block.term != Return || block.stmts.length() > limit { continue }
    if block.stmts.iter().any(fn(s) { s is Call(_) }) { continue }
    leaves[f.name] = f
  }
  let mut count = 0
  for f in self.functions {
    if f.native { continue }
    for block in f.blocks {
      if !block.stmts.iter().any(fn(s) { s is Call(g) && leaves.contains(g) }) { continue }
      let out = []
      for s in block.stmts {
        match s {
          Call(g) if leaves.get(g) is Some(leaf) && g != f.name => {
            let temps = leaf.temps.map(fn(t) { f.new_temp(t.hint, t.width) })
            let remap = fn(e : Expr) -> Expr {
              let mut e = e
              for k, t in temps { e = e.replace_temp(k, Temp(-100 - t)) }
              for t in temps { e = e.replace_temp(-100 - t, Temp(t)) }
              e
            }
            for inner in leaf.blocks[0].stmts {
              out.push(
                match inner {
                  Let(t, e) => Let(temps[t], remap(e))
                  other => other.map_exprs(remap)
                },
              )
            }
            count += 1
          }
          other => out.push(other)
        }
      }
      block.stmts = out
    }
  }
  count
}
