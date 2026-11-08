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
  summary : WebSummary
}

// What a routine does with its parameters and gives back as its results,
// for names from its callees (`derive_names`), by register: what each
// parameter's entry value is passed to (callee, register) and whether
// anything else reads it but a return; the callees whose results its
// returns give back, and whether anything else is.
priv struct WebSummary {
  entry_passes : Array[Array[(String, Int)]]
  entry_plain : Array[Bool]
  result_calls : Array[Array[String]]
  result_other : Array[Bool]
  // the result is (also) reloaded from a `param *` byte (X from `slot`)
  result_loaded : Array[Bool]
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
  match conv.derived_params.get(f) {
    Some(names) if names[r] != "" => return Some((names[r], ""))
    _ => ()
  }
  if explicit { return None }
  let entered = conv.entered.get(f).unwrap_or([])
  for d in views.default_params {
    if d.0 == r && entered.iter().any(fn(e) { e.0 == r && e.1 == d.2 }) { return Some((d.1, d.3)) }
  }
  None
}

fn map_result(views : Views, conv : Convention, f : String, r : Int) -> (String, String)? {
  match views.results.get(f) {
    Some(list) =>
      match list.iter().find_first(fn(p) { p.0 == r }) {
        Some(p) => return Some((p.1, p.2))
        None => ()
      }
    None => ()
  }
  match conv.derived_results.get(f) {
    Some(names) if names[r] != "" => Some((names[r], ""))
    _ => None
  }
}

// The names a register's variable in `f` must not take.
// (besides those the views hold: see `Views::taken`)
fn names_taken(views : Views, f : @ir.Function, conv : Convention) -> Set[String] {
  let taken : Set[String] = Set([])
  for name in local_names { taken.add(name) }
  for name in temp_names(f, locals=true) { taken.add(name) }
  let plan = scratch_plan_named(f, views, conv)
  for name in plan.ram { if name != "" { taken.add(name) } }
  for name in plan.locals { taken.add(name) }
  taken
}

// Whether a routine's registers are named.
fn Convention::nameable(self : Convention, f : @ir.Function) -> Bool {
  self.has_locals(f.name) && !f.helper
}

// The names of scratch values the map leaves unnamed: a value a routine
// passes to callees that take it by value, the name of their parameter,
// when they agree (`apply_gravity_object` passes its caller's forces on to
// `apply_gravity`'s `down_force` and `up_force`). Names go up the calls
// until none changes.
fn Convention::derive_scratch_names(self : Convention, program : @ir.Program, views : Views) -> Unit {
  let map_name = fn(g : String, k : Int) -> String {
    match views.scratch.get(g) {
      Some(list) =>
        match list.iter().find_first(fn(e) { e.0 == k }) {
          Some(e) => e.1
          None => ""
        }
      None => ""
    }
  }
  let scratch_names : Set[String] = Set([])
  for _, list in views.scratch {
    for e in list { scratch_names.add(e.1) }
  }
  for f in program.functions {
    if self.webs.contains(f.name) { self.scratch_names[f.name] = Array::make(scratch_bytes, "") }
  }
  let name_in = fn(g : String, k : Int) {
    let given = map_name(g, k)
    if given != "" { given } else { self.scratch_names.get(g).map(fn(a) { a[k] }).unwrap_or("") }
  }
  let mut changed = true
  let mut rounds = 0
  while changed && rounds < 32 {
    changed = false
    rounds += 1
    for f in program.functions {
      guard self.webs.get(f.name) is Some(webs) else { continue }
      let own : Set[String] = Set([])
      for k in 0..<scratch_bytes { if map_name(f.name, k) != "" { own.add(map_name(f.name, k)) } }
      for k in 0..<scratch_bytes {
        if map_name(f.name, k) != "" { continue }
        let bit = scratch_bit(k)
        let names = []
        for b, block in f.blocks {
          for i, s in block.stmts {
            match s {
              Call(g) if (self.sig(g).promoted & bit) != 0 && (webs.uses[b][i] & (1 << k)) != 0 => names.push(name_in(g, k))
              _ => ()
            }
          }
          match block.term {
            TailCall(g) if (self.sig(g).promoted & bit) != 0 && (webs.terms[b] & (1 << k)) != 0 => names.push(name_in(g, k))
            _ => ()
          }
        }
        let name = if !names.is_empty() && names[0] != "" && names.iter().all(fn(n) { n == names[0] }) { names[0] } else { "" }
        // (not a name the code has, but the scratch names, nor another of
        // the routine's bytes')
        let fits = name != "" && (!views.taken.contains(name) || scratch_names.contains(name)) && !own.contains(name) &&
          !self.scratch_names[f.name].iter().any(fn(n) { n == name })
        let next = if fits { name } else { "" }
        if self.scratch_names[f.name][k] != next && (next != "" || self.scratch_names[f.name][k] == "") {
          self.scratch_names[f.name][k] = next
          changed = true
        }
      }
    }
  }
}

// Names from the callees: a parameter whose entry value is only passed to
// callees' named parameters (and given back) takes their name, and a result
// that is only callees' named results takes theirs, up the calls until none
// changes (`move_enemy_x` gives back `move_object_x`'s `pixels_moved`).
fn Convention::derive_names(self : Convention, program : @ir.Program, views : Views) -> Unit {
  let summaries : Map[String, WebSummary] = Map([])
  for f in program.functions {
    if !self.nameable(f) { continue }
    let plan = scratch_plan_named(f, views, self)
    match new_cfunc(f, views, self, plan).register_names() {
      Some(n) => summaries[f.name] = n.summary
      None => ()
    }
    self.derived_params[f.name] = Array::make(7, "")
    self.derived_results[f.name] = Array::make(7, "")
  }
  let agreed = fn(list : Array[String]) -> String {
    if list.is_empty() || list[0] == "" { return "" }
    if list.iter().all(fn(x) { x == list[0] }) { list[0] } else { "" }
  }
  let mut changed = true
  let mut rounds = 0
  while changed && rounds < 32 {
    changed = false
    rounds += 1
    for name, summary in summaries {
      let sig = self.sig(name)
      for r in 0..<7 {
        let bit = held_vars[r].bit()
        if (sig.params & bit) != 0 && map_param(views, self, name, r, explicit=true) is None && !summary.entry_plain[r] {
          let derived = agreed(summary.entry_passes[r].map(fn(p) { match map_param(views, self, p.0, p.1, explicit=true) { Some((n, _)) => n; None => "" } }))
          if derived != "" && self.derived_params[name][r] == "" {
            self.derived_params[name][r] = derived
            changed = true
          }
        }
        if (sig.outputs & bit) != 0 && !summary.result_other[r] && map_result(views, self, name, r) is None {
          let names = summary.result_calls[r].map(fn(g) { match map_result(views, self, g, r) { Some((n, _)) => n; None => "" } })
          // (a result reloaded from `slot` is `object`)
          if summary.result_loaded[r] {
            match views.default_params.iter().find_first(fn(d) { d.0 == r }) {
              Some(d) => names.push(d.1)
              None => names.push("")
            }
          }
          let derived = agreed(names)
          if derived != "" {
            self.derived_results[name][r] = derived
            changed = true
          }
        }
      }
    }
  }
}

// The names of each routine's parameters and results (in its prototype).
fn Convention::name_parameters(self : Convention, program : @ir.Program, views : Views) -> Unit {
  self.derive_scratch_names(program, views)
  self.derive_names(program, views)
  for f in program.functions {
    if !self.nameable(f) { continue }
    let sig = self.sig(f.name)
    let taken = names_taken(views, f, self)
    let names : Map[Int, String] = Map([])
    let used : Set[String] = Set([])
    for s in slots_of((sig.value_bits() | sig.pointer_bits()) & register_bits) {
      guard s is Reg(v) else { continue }
      let r = held_index(v)
      let named = if (sig.params & s.bit()) != 0 { map_param(views, self, f.name, r) } else { map_result(views, self, f.name, r) }
      match named {
        Some((name, _)) if !taken.contains(name) && !views.taken.contains(name) && !used.contains(name) => {
          names[s.bit()] = name
          used.add(name)
        }
        _ => ()
      }
    }
    // the scratch bytes it takes by value: its locals' names
    if sig.promoted != 0 {
      let plan = scratch_plan_named(f, views, self)
      for k in 0..<scratch_bytes {
        if (sig.promoted & scratch_bit(k)) != 0 { names[scratch_bit(k)] = plan.locals[k] }
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
            results[root].push(match map_result(self.views, conv, g, r) { Some((name, _)) => name; None => "" })
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
  // what the routine does with its parameters and gives back
  let passes : Array[Array[(String, Int)]] = Array::makei(n, fn(_) { [] })
  let plain = Array::make(n, false)
  let def_calls : Array[Array[String]] = Array::makei(n, fn(_) { [] })
  let tail_results : Array[Array[String]] = Array::makei(7, fn(_) { [] })
  for b, block in f.blocks {
    for k, s in block.stmts {
      for r in 0..<7 {
        let u = use_node[b][k][r]
        if u >= 0 {
          match s.callee() {
            Some(g) => passes[web.find(u)].push((g, r))
            None => plain[web.find(u)] = true
          }
        }
        let d = def_node[b][k][r]
        if d >= 0 && s.callee() is Some(g) { def_calls[web.find(d)].push(g) }
      }
    }
    let t = block.term
    let read_plain = match t {
      Branch(_, _, _) | Switch(_, _, _) | ReturnValue(_) | Jump(_, _) => term_uses(t)
      Dispatch(h, _, _) => conv.sig(h).params
      _ => 0
    }
    let pass = fn(g : String, wrote : Int) {
      for r in 0..<7 {
        let node = term_node[b][r]
        if node >= 0 && (conv.sig(g).params & wrote.lnot() & held_vars[r].bit()) != 0 { passes[web.find(node)].push((g, r)) }
        if (own.outputs & conv.sig(g).outputs & wrote.lnot() & held_vars[r].bit()) != 0 { tail_results[r].push(g) }
      }
    }
    match t {
      TailCall(g) => pass(g, 0)
      Jump(_, cases) => for c in cases { pass(c.1, 0) }
      Dispatch(h, _, cases) => for c in cases { pass(c.1, conv.dispatch_writes(h)) }
      _ => ()
    }
    for r in 0..<7 {
      let node = term_node[b][r]
      if node >= 0 && (read_plain & held_vars[r].bit()) != 0 { plain[web.find(node)] = true }
    }
  }
  let result_calls : Array[Array[String]] = Array::makei(7, fn(r) { tail_results[r].copy() })
  let result_other = Array::make(7, false)
  let result_loaded = Array::make(7, false)
  for node in 0..<n {
    if web.find(node) != node || !returned[node] { continue }
    let r = web.reg[node]
    if node < 7 || others[node] > 0 { result_other[r] = true }
    if loads[node] > 0 { result_loaded[r] = true }
    for g in def_calls[node] { result_calls[r].push(g) }
  }
  let summary : WebSummary = {
    entry_passes: Array::makei(7, fn(r) { passes[web.find(r)] }),
    entry_plain: Array::makei(7, fn(r) { plain[web.find(r)] }),
    result_calls,
    result_other,
    result_loaded,
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
    } else if returned[node] && map_result(self.views, conv, f.name, r) is Some((name, _)) {
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
        } else if !taken.contains(name) && !self.views.taken.contains(name) && owner.get(name).unwrap_or(r) == r {
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
  // what a tail call writes: the callee's result names (the return reads
  // them there)
  let term_defs = f.blocks.map(fn(block) {
    let out = Array::makei(7, fn(r) { local_names[r] })
    if block.term is TailCall(g) {
      for r in 0..<7 {
        if (conv.sig(g).outputs & held_vars[r].bit()) == 0 { continue }
        match map_result(self.views, conv, g, r) {
          Some((given, _)) if !taken.contains(given) && !self.views.taken.contains(given) && owner.get(given).unwrap_or(r) == r => {
            owner[given] = r
            out[r] = given
          }
          _ => ()
        }
      }
    }
    out
  })
  let defs_out = f.blocks.mapi(fn(b, block) {
    [term_defs[b]] + block.stmts.mapi(fn(k, _) { Array::makei(7, fn(r) { name(def_node[b][k][r], r) }) })
  })
  for b, _ in f.blocks {
    for r in 0..<7 { note(term_defs[b][r], r) }
  }
  for node in 0..<n {
    if web.find(node) == node && name_of[node] != "" { note(name_of[node], web.reg[node]) }
  }
  Some({ uses: uses_out, defs: defs_out, names: all, summary })
}
