// Each value a register holds, its own variable, named for what it is.
//
// A routine reuses its registers for one value after another: in
// `apply_gravity`, A is first whether the object may rise, then its speed,
// then the speed with the force added. With locals (locals.mbt) a register
// is one C variable, `a`, which a parameter's name could not stay on.
//
// Here the writes of each register are joined with the reads they reach
// (its webs, as the scratch bytes' in `webs_of`), where it is live: a web
// is one value and can be its own variable. A web is named, in this order,
// for:
//
// 1. the routine's parameter, if the routine's entry is one of its writes:
//    the map's `param` line, or `param * x object slot` in a routine that
//    every call enters with X equal to `slot` (also a web of X written only
//    by loads of `slot`);
// 2. the routine's result, if a return reads it (the map's `result`);
// 3. the result of the calls that write it, if they agree;
// 4. the parameter of the calls it is passed to, if they agree (the map's
//    `param` lines: a routine's `object` is the object slot it processes,
//    which its caller's may not be).
//
// The webs left unnamed share their register's variable (`a`, `x`, `carry`),
// as before, and so do those whose name is taken (a view, a record, a
// routine, a temporary, a scratch name, or another register's variable).

// The registers and flags that are locals, in `local_names` order.
let register_bits : Int = local_vars

priv struct RegNames {
  // The variable a register's read at a statement (stmt + 1; 0: the
  // terminator) is of, and the one its write there is to.
  uses : Array[Array[Array[String]]]
  defs : Array[Array[Array[String]]]
  // Every variable, with its register (by `local_names` index).
  names : Array[(String, Int)]
}

fn reg_slot(r : Int) -> Slot {
  Reg(held_vars[r])
}

// The map's name for register r's parameter of routine `f`: its `param`
// line, or a `param *` line when every call enters it with r equal to the
// line's byte.
fn map_param(views : Views, conv : Convention, f : String, r : Int, explicit~ : Bool = false) -> (String, String)? {
  match views.params.get(f) {
    Some(list) =>
      match list.iter().find_first(fn(p) { p.0 == r }) {
        Some(p) => return Some((p.1, p.2))
        None => ()
      }
    None => ()
  }
  if explicit { return None }
  let entered = conv.entered.get(f).unwrap_or([])
  for d in views.default_params {
    if d.0 == r && entered.iter().any(fn(e) { e.0 == r && e.1 == d.2 }) { return Some((d.1, d.3)) }
  }
  None
}

fn map_result(views : Views, f : String, r : Int) -> (String, String)? {
  match views.results.get(f) {
    Some(list) => list.iter().find_first(fn(p) { p.0 == r }).map(fn(p) { (p.1, p.2) })
    None => None
  }
}

// The names a register's variable in `f` must not take.
fn names_taken(views : Views, f : @ir.Function, conv : Convention) -> Set[String] {
  let taken : Set[String] = Set([])
  for name in views.taken { taken.add(name) }
  for name in local_names { taken.add(name) }
  for name in temp_names(f, locals=true) { taken.add(name) }
  let plan = scratch_plan(f, views, conv)
  for name in plan.ram { if name != "" { taken.add(name) } }
  for name in plan.locals { taken.add(name) }
  taken
}

// Whether a routine's registers are named.
fn Convention::nameable(self : Convention, f : @ir.Function) -> Bool {
  self.has_locals(f.name) && !f.helper
}

// The names of each routine's parameters and results (in its prototype).
fn Convention::name_parameters(self : Convention, program : @ir.Program, views : Views) -> Unit {
  for f in program.functions {
    if !self.nameable(f) { continue }
    let sig = self.sig(f.name)
    let taken = names_taken(views, f, self)
    let names : Map[Int, String] = Map([])
    let used : Set[String] = Set([])
    for s in slots_of((sig.value_bits() | sig.pointer_bits()) & register_bits) {
      guard s is Reg(v) else { continue }
      let r = held_index(v)
      let named = if (sig.params & s.bit()) != 0 { map_param(views, self, f.name, r) } else { map_result(views, f.name, r) }
      match named {
        Some((name, _)) if !taken.contains(name) && !used.contains(name) => {
          names[s.bit()] = name
          used.add(name)
        }
        _ => ()
      }
    }
    self.param_names[f.name] = names
  }
}

// The registers a terminator reads: its condition or target, the results a
// return reads, a tail call's parameters and the results it leaves.
fn Convention::term_reads(self : Convention, f : @ir.Function, term : @ir.Term) -> Int {
  let own = self.sig(f.name)
  let tail = fn(g : String) {
    let s = self.sig(g)
    s.params | (own.outputs & s.outputs.lnot())
  }
  let bits = match term {
    Branch(_, _, _) | Switch(_, _, _) => term_uses(term)
    Return => own.outputs
    ReturnValue(_) => own.outputs | term_uses(term)
    TailCall(g) => tail(g)
    Jump(_, cases) => cases.fold(init=term_uses(term), fn(acc, c) { acc | tail(c.1) })
    // (the cases read what the dispatcher writes from the registers' own
    // variables)
    Dispatch(h, _, cases) => self.sig(h).params | (cases.fold(init=0, fn(acc, c) { acc | tail(c.1) }) & self.dispatch_writes(h).lnot())
    _ => 0
  }
  bits & register_bits
}

// What a dispatcher writes to the registers' own variables (see `writes_fresh`).
fn Convention::dispatch_writes(self : Convention, h : String) -> Int {
  let s = self.sig(h)
  let back = if self.has_locals(h) { s.pointer_bits() & s.params } else { 0 }
  s.outputs & back.lnot()
}

priv struct Web {
  parent : Array[Int]
  reg : Array[Int]
}

fn Web::add(self : Web, r : Int) -> Int {
  self.parent.push(self.parent.length())
  self.reg.push(r)
  self.parent.length() - 1
}

fn Web::find(self : Web, n : Int) -> Int {
  let mut r = n
  while self.parent[r] != r { r = self.parent[r] }
  self.parent[n] = r
  r
}

// Joins two webs: whether they were apart.
fn Web::union(self : Web, a : Int, b : Int) -> Bool {
  let ra = self.find(a)
  let rb = self.find(b)
  if ra == rb { return false }
  // (the lower node, an entry's when there is one, stays the root)
  if ra < rb { self.parent[rb] = ra } else { self.parent[ra] = rb }
  true
}

// The registers' variables in this routine (see the top of the file).
fn CFunc::register_names(self : CFunc) -> RegNames? {
  let f = self.f
  if !self.locals || !self.conv.nameable(f) { return None }
  let conv = self.conv
  let own = conv.sig(f.name)
  let same_x = conv.same_x.get(f.name).unwrap_or(Set([]))
  let web : Web = { parent: [], reg: [] }
  for r in 0..<7 { ignore(web.add(r)) }
  // each statement's reads, writes and the results written through a
  // pointer it also reads (which stay one variable)
  let uses = f.blocks.map(fn(b) { b.stmts.map(fn(_) { 0 }) })
  let defs = f.blocks.map(fn(b) { b.stmts.map(fn(_) { 0 }) })
  let inout = f.blocks.map(fn(b) { b.stmts.map(fn(_) { 0 }) })
  let def_node = f.blocks.map(fn(b) { b.stmts.map(fn(_) { Array::make(7, -1) }) })
  for b, block in f.blocks {
    for k, s in block.stmts {
      let (u, d, io) = match s {
        Call(g) | CallValue(_, g, _) => {
          let sg = conv.sig(g)
          let mut d = sg.outputs & register_bits
          // (X back as it was: the call assigns no X)
          if same_x.contains((b << 16) | k) && sg.primary is Some(Reg(X)) { d = d & held_vars[1].bit().lnot() }
          let io = if conv.has_locals(g) { sg.pointer_bits() & sg.params & register_bits } else { 0 }
          (sg.params & register_bits, d, io)
        }
        _ =>
          if dead(s, self.after[b][k], self.memory) {
            (0, 0, 0)
          } else {
            (stmt_uses(s) & register_bits, stmt_defs(s) & register_bits, 0)
          }
      }
      uses[b][k] = u
      defs[b][k] = d
      inout[b][k] = io
      for r in 0..<7 {
        if (d & held_vars[r].bit()) != 0 { def_node[b][k][r] = web.add(r) }
      }
    }
  }
  let term_bits = f.blocks.map(fn(b) { conv.term_reads(f, b.term) })
  // the web each read is of, joined where the register is live
  let use_node = f.blocks.map(fn(b) { b.stmts.map(fn(_) { Array::make(7, -1) }) })
  let term_node = f.blocks.map(fn(_) { Array::make(7, -1) })
  let preds : Array[Array[Int]] = f.blocks.map(fn(_) { [] })
  for b, block in f.blocks {
    for t in block.term.successors() { preds[t].push(b) }
  }
  // where each register is live, by these reads and writes (a call that
  // gives X back as it was does not write it)
  let live_in = f.blocks.map(fn(_) { 0 })
  let mut moving = true
  while moving {
    moving = false
    let mut b = f.blocks.length() - 1
    while b >= 0 {
      let block = f.blocks[b]
      let mut l = block.term.successors().fold(init=term_bits[b], fn(acc, t) { acc | live_in[t] })
      let mut k = block.stmts.length() - 1
      while k >= 0 {
        l = (l & defs[b][k].lnot()) | uses[b][k]
        k -= 1
      }
      if l != live_in[b] {
        live_in[b] = l
        moving = true
      }
      b -= 1
    }
  }
  let order = reverse_postorder(f)
  let ends : Array[Array[Int]] = f.blocks.map(fn(_) { Array::make(7, -1) })
  let mut changed = true
  while changed {
    changed = false
    for b in order {
      let cur = Array::make(7, -1)
      for r in 0..<7 {
        let bit = held_vars[r].bit()
        if b == f.entry { cur[r] = r }
        if b != f.entry && (live_in[b] & bit) == 0 { continue }
        for p in preds[b] {
          let e = ends[p][r]
          if e < 0 { continue }
          if cur[r] < 0 { cur[r] = e } else if web.union(cur[r], e) { changed = true }
        }
      }
      for k, _ in f.blocks[b].stmts {
        for r in 0..<7 {
          let bit = held_vars[r].bit()
          if (uses[b][k] & bit) != 0 {
            // (a read nothing wrote: of what the routine was entered with)
            let u = if cur[r] >= 0 { cur[r] } else { r }
            use_node[b][k][r] = u
          }
        }
        for r in 0..<7 {
          let d = def_node[b][k][r]
          if d < 0 { continue }
          if (inout[b][k] & held_vars[r].bit()) != 0 && use_node[b][k][r] >= 0 {
            if web.union(d, use_node[b][k][r]) { changed = true }
          }
          cur[r] = d
        }
      }
      for r in 0..<7 {
        if (term_bits[b] & held_vars[r].bit()) != 0 { term_node[b][r] = if cur[r] >= 0 { cur[r] } else { r } }
      }
      for r in 0..<7 {
        if ends[b][r] != cur[r] {
          ends[b] = cur
          changed = true
          break
        }
      }
    }
  }
  // what each web is
  let n = web.parent.length()
  let entry_param = Array::make(n, false)
  for r in 0..<7 {
    if (own.params & held_vars[r].bit()) != 0 { entry_param[web.find(r)] = true }
  }
  let default_var = fn(r : Int) {
    self.views.default_params.iter().find_first(fn(d) { d.0 == r })
  }
  // writes: loads of a `param *` byte, call results, and others
  let loads = Array::make(n, 0)
  let others = Array::make(n, 0)
  let results : Array[Array[String]] = Array::makei(n, fn(_) { [] })
  for b, block in f.blocks {
    for k, s in block.stmts {
      for r in 0..<7 {
        let d = def_node[b][k][r]
        if d < 0 { continue }
        let root = web.find(d)
        match s {
          Call(g) | CallValue(_, g, _) =>
            results[root].push(match map_result(self.views, g, r) { Some((name, _)) => name; None => "" })
          Assign(_, Load(Ram, a)) if default_var(r) is Some(dv) && a.value() == Some(dv.2) => loads[root] += 1
          _ => others[root] += 1
        }
      }
    }
  }
  // reads: at a return, as arguments
  let returned = Array::make(n, false)
  let args : Array[Array[String]] = Array::makei(n, fn(_) { [] })
  for b, block in f.blocks {
    for k, s in block.stmts {
      guard s.callee() is Some(g) else { continue }
      for r in 0..<7 {
        let u = use_node[b][k][r]
        if u < 0 { continue }
        args[web.find(u)].push(match map_param(self.views, conv, g, r, explicit=true) { Some((name, _)) => name; None => "" })
      }
    }
    let leaves = match block.term {
      Return | ReturnValue(_) => own.outputs
      TailCall(g) => own.outputs & conv.sig(g).outputs.lnot()
      Jump(_, cases) => cases.fold(init=0, fn(acc, c) { acc | (own.outputs & conv.sig(c.1).outputs.lnot()) })
      Dispatch(h, _, cases) =>
        cases.fold(init=0, fn(acc, c) { acc | (own.outputs & conv.sig(c.1).outputs.lnot()) }) & conv.dispatch_writes(h).lnot()
      _ => 0
    }
    for r in 0..<7 {
      let t = term_node[b][r]
      if t >= 0 && (leaves & held_vars[r].bit()) != 0 { returned[web.find(t)] = true }
    }
    let passed = fn(g : String, wrote : Int) {
      for r in 0..<7 {
        let t = term_node[b][r]
        if t >= 0 && (conv.sig(g).params & wrote.lnot() & held_vars[r].bit()) != 0 {
          args[web.find(t)].push(match map_param(self.views, conv, g, r, explicit=true) { Some((name, _)) => name; None => "" })
        }
      }
    }
    match block.term {
      TailCall(g) => passed(g, 0)
      Jump(_, cases) => for c in cases { passed(c.1, 0) }
      Dispatch(h, _, cases) => for c in cases { passed(c.1, conv.dispatch_writes(h)) }
      _ => ()
    }
  }
  let agreed = fn(list : Array[String]) -> String? {
    if list.is_empty() || list[0] == "" { return None }
    if list.iter().all(fn(x) { x == list[0] }) { Some(list[0]) } else { None }
  }
  // the names, unless taken or another register's
  let taken = names_taken(self.views, f, conv)
  let owner : Map[String, Int] = Map([])
  for bit, name in conv.param_names.get(f.name).unwrap_or(Map([])) {
    for r in 0..<7 {
      if held_vars[r].bit() == bit { owner[name] = r }
    }
    // (a pointer's own name)
    if (own.pointer_bits() & bit) != 0 {
      for s in slots_of(bit) { taken.add(conv.pointer_param(f.name, s)) }
    }
  }
  let name_of : Array[String] = Array::make(n, "")
  for node in 0..<n {
    if web.find(node) != node { continue }
    let r = web.reg[node]
    let slot = reg_slot(r)
    let candidate = if entry_param[node] {
      conv.param_names.get(f.name).unwrap_or(Map([])).get(slot.bit())
    } else if loads[node] > 0 && others[node] == 0 && results[node].is_empty() && default_var(r) is Some(dv) {
      Some(dv.1)
    } else if returned[node] && map_result(self.views, f.name, r) is Some((name, _)) {
      Some(name)
    } else if agreed(results[node]) is Some(name) {
      Some(name)
    } else {
      agreed(args[node])
    }
    match candidate {
      Some(name) =>
        if entry_param[node] {
          name_of[node] = name
        } else if !taken.contains(name) && owner.get(name).unwrap_or(r) == r {
          owner[name] = r
          name_of[node] = name
        }
      None => ()
    }
  }
  let name = fn(node : Int, r : Int) {
    if node < 0 { return local_names[r] }
    let given = name_of[web.find(node)]
    if given == "" { local_names[r] } else { given }
  }
  let all : Array[(String, Int)] = []
  let seen : Set[String] = Set([])
  let note = fn(text : String, r : Int) {
    if !seen.contains(text) {
      seen.add(text)
      all.push((text, r))
    }
  }
  for r in 0..<7 { note(local_names[r], r) }
  let uses_out = f.blocks.mapi(fn(b, block) {
    [Array::makei(7, fn(r) { name(term_node[b][r], r) })] + block.stmts.mapi(fn(k, _) { Array::makei(7, fn(r) { name(use_node[b][k][r], r) }) })
  })
  let defs_out = f.blocks.mapi(fn(b, block) {
    [Array::makei(7, fn(r) { local_names[r] })] + block.stmts.mapi(fn(k, _) { Array::makei(7, fn(r) { name(def_node[b][k][r], r) }) })
  })
  for node in 0..<n {
    if web.find(node) == node && name_of[node] != "" { note(name_of[node], web.reg[node]) }
  }
  Some({ uses: uses_out, defs: defs_out, names: all })
}
