// C backend: prints the IR as structured C.
//
// Blocks keep their original order. Forward branches over single-entry
// regions become if/else, conditional back edges closing single-entry
// regions become do/while loops, and anything else stays a goto. Blocks
// that only return, tail-call, yield or trap are printed where they are
// reached instead of being laid out.

fn show_number(v : Int) -> String {
  if v >= 0 && v < 10 { v.to_string() } else if v < 0 { "-0x" + hex(-v) } else { "0x" + hex(v) }
}

// Precedence levels, lower binds tighter.
fn bin_prec(op : @ir.BinOp) -> Int {
  match op {
    Shl | Shr => 5
    Add | Sub => 4
    Lt | Ge => 6
    Eq | Ne => 7
    And => 8
    Xor => 9
    Or => 10
  }
}

fn bin_symbol(op : @ir.BinOp) -> String {
  match op {
    Add => "+"; Sub => "-"; And => "&"; Or => "|"; Xor => "^"
    Shl => "<<"; Shr => ">>"; Eq => "=="; Ne => "!="; Lt => "<"; Ge => ">="
  }
}

priv struct CFunc {
  f : @ir.Function
  names : Array[String]
  views : Views
}

fn temp_names(f : @ir.Function) -> Array[String] {
  let seen : Map[String, Int] = Map([])
  f.temps.map(fn(t) {
    let n = seen.get(t.hint).unwrap_or(0) + 1
    seen[t.hint] = n
    if n == 1 { t.hint } else { t.hint + n.to_string() }
  })
}

// `(uint8_t)` is needed unless the value is evidently a byte (clang's
// -Wconversion reasoning: loads, byte variables, masks, comparisons).
fn CFunc::narrow(self : CFunc, e : @ir.Expr) -> Bool {
  match e {
    Const(v) | Sym(_, v) => v >= 0 && v <= 255
    Var(_) => true
    Temp(t) => self.f.temps[t].width == U8 || self.f.temps[t].width == Flag
    Load(_, _) => true
    Unary(_, _) => true
    Binary(Eq | Ne | Lt | Ge, _, _) => true
    Binary(And, l, r) => self.narrow(l) || self.narrow(r)
    Binary(Or | Xor, l, r) => self.narrow(l) && self.narrow(r)
    Binary(Shr, l, Const(_)) => self.narrow(l)
    _ => false
  }
}

fn CFunc::narrow16(self : CFunc, e : @ir.Expr) -> Bool {
  match e {
    Const(v) | Sym(_, v) => v >= 0 && v <= 0xffff
    Temp(t) => self.f.temps[t].width != Wide
    Param => true
    _ => self.narrow(e) || self.indirect(e) is Some(_)
  }
}

// `indirect_y_addr(p)` and `indirect_x_addr(p)` for pointer reads.
fn CFunc::indirect(self : CFunc, e : @ir.Expr) -> String? {
  fn pointer(e : @ir.Expr) -> @ir.Expr? {
    match e {
      Binary(Or, Load(Ram, p), Binary(Shl, Load(Ram, q), Const(8))) => {
        let next = match p.value() {
          Some(v) if v + 1 <= 0xff => @ir.Expr::Binary(Add, p, Const(1))
          Some(_) => Const(0)
          None => Unary(Byte, Binary(Add, p, Const(1)))
        }
        if @ir.simplify(next) == @ir.simplify(q) { Some(p) } else { None }
      }
      _ => None
    }
  }
  match e {
    Binary(Add, base, Var(Y)) =>
      match pointer(base) {
        Some(p) if p.is_atom() => Some("indirect_y_addr(\{self.expr(p)})")
        _ => None
      }
    _ =>
      match pointer(e) {
        Some(Unary(Byte, Binary(Add, p, Var(X)))) if p.is_atom() => Some("indirect_x_addr(\{self.expr(p)})")
        _ => None
      }
  }
}

// A memory access by the name of what it reads or writes, or by address.
fn CFunc::access(self : CFunc, space : @ir.Mem, a : @ir.Expr) -> String {
  match self.views.place(a) {
    Some({ view, offset, index }) if (view.kind == Table) == (space == Rom) => {
      if view.kind == Scalar { return view.name }
      let terms = index.map(fn(e) { self.paren(e, 4) }).join(" + ")
      let at = if index.is_empty() {
        show_number(offset)
      } else if offset > 0 {
        "\{show_number(offset)} + \{terms}"
      } else if offset < 0 {
        "\{terms} - \{show_number(-offset)}"
      } else {
        terms
      }
      match view.kind {
        ZeroPageArray | MirroredArray => "\{view.name}(\{at})"
        _ => "\{view.name}[\{at}]"
      }
    }
    _ =>
      match a {
        Binary(Add, Const(0x100), i) if space == Ram && self.views.stack => "stack[\{self.expr(i)}]"
        _ => if space == Rom { "ROM(\{self.expr(a)})" } else { "ram[\{self.expr(a)}]" }
      }
  }
}

// The map type of a value: a typed variable's, kept by masking and
// combining it with others of its type.
fn CFunc::type_of(self : CFunc, e : @ir.Expr) -> String {
  match e {
    Load(Ram, a) =>
      match self.views.place(a) {
        Some({ view, .. }) => view.type_name
        None => ""
      }
    Binary(And | Or | Xor, l, r) => {
      let t = self.type_of(l)
      if t != "" { t } else { self.type_of(r) }
    }
    _ => ""
  }
}

// `e`, a constant of the type `type_name` by name.
fn CFunc::typed(self : CFunc, e : @ir.Expr, type_name : String, mask~ : Bool = false) -> String? {
  match e {
    Const(v) if type_name != "" => self.views.constant(type_name, v, mask~)
    _ => None
  }
}

fn CFunc::expr(self : CFunc, e : @ir.Expr) -> String {
  self.expr_prec(e).0
}

fn CFunc::paren(self : CFunc, e : @ir.Expr, max : Int) -> String {
  let (text, prec) = self.expr_prec(e)
  if prec > max { "(" + text + ")" } else { text }
}

fn CFunc::byte(self : CFunc, e : @ir.Expr) -> String {
  if self.narrow(e) { self.expr(e) } else { "(uint8_t)" + self.paren(e, 0) }
}

fn CFunc::word(self : CFunc, e : @ir.Expr) -> String {
  match self.indirect(e) {
    Some(text) => text
    None => if self.narrow16(e) { self.expr(e) } else { "(uint16_t)" + self.paren(e, 0) }
  }
}

fn CFunc::expr_prec(self : CFunc, e : @ir.Expr) -> (String, Int) {
  match e {
    Const(v) => (show_number(v), 0)
    Sym(name, _) => (name, 0)
    Var(v) => (v.c_name(), 0)
    Temp(t) => (self.names[t], 0)
    Param => ("return_address", 0)
    Load(Ram | Rom as space, a) => (self.access(space, a), 0)
    Load(Bus, a) => ("read_byte(\{self.word(a)})", 0)
    Unary(Not, a) => ("!" + self.paren(a, 2), 2)
    Unary(Byte, a) => ("(uint8_t)" + self.paren(a, 0), 2)
    Unary(Lo, a) => ("LOW_BYTE(\{self.expr(a)})", 0)
    Unary(Hi, a) => ("HIGH_BYTE(\{self.expr(a)})", 0)
    Binary(op, l, r) => {
      let p = bin_prec(op)
      // a constant beside a typed value is a value of that type
      let typed = match op {
        Eq | Ne | Lt | Ge | And | Or | Xor =>
          match (self.typed(r, self.type_of(l), mask=op == And), self.typed(l, self.type_of(r), mask=op == And)) {
            (Some(name), _) => Some((l, name, false))
            (_, Some(name)) => Some((r, name, true))
            _ => None
          }
        _ => None
      }
      if typed is Some((other, name, left)) {
        let name = if name.contains(" ") { "(" + name + ")" } else { name }
        let side = match other {
          Binary(cop, _, _) if cop != op => "(" + self.expr(other) + ")"
          _ => self.paren(other, p)
        }
        return (if left { "\{name} \{bin_symbol(op)} \{side}" } else { "\{side} \{bin_symbol(op)} \{name}" }, p)
      }
      let side = fn(child : @ir.Expr, left : Bool) -> String {
        match child {
          Binary(cop, _, _) => {
            let same = cop == op
            let associative = match op {
              Add | And | Or | Xor => true
              _ => false
            }
            // a chain of the same operator reads naturally; mixed
            // operators get parentheses for clarity
            if (same && (associative || left)) || (left && (op == Add || op == Sub) && (cop == Add || cop == Sub)) {
              self.paren(child, p)
            } else {
              "(" + self.expr(child) + ")"
            }
          }
          _ => self.paren(child, p)
        }
      }
      ("\{side(l, true)} \{bin_symbol(op)} \{side(r, false)}", p)
    }
  }
}

fn CFunc::assign(self : CFunc, v : @ir.Var, e : @ir.Expr) -> String {
  let name = v.c_name()
  if v.is_flag() {
    return match e {
      Const(0) => "\{name} = false;"
      Const(1) => "\{name} = true;"
      _ => "\{name} = \{self.expr(e)};"
    }
  }
  match e {
    Binary(Add, Var(u), Const(1)) if u == v => "\{name}++;"
    Binary(Sub, Var(u), Const(1)) if u == v => "\{name}--;"
    Binary(And | Or | Xor as op, Var(u), r) if u == v && self.narrow(r) =>
      "\{name} \{bin_symbol(op)}= \{self.expr(r)};"
    _ => "\{name} = \{self.byte(e)};"
  }
}

fn CFunc::stmt(self : CFunc, s : @ir.Stmt) -> String {
  match s {
    Assign(v, e) => self.assign(v, e)
    Let(t, e) => {
      let name = self.names[t]
      match self.f.temps[t].width {
        U8 => "uint8_t \{name} = \{self.byte(e)};"
        U16 => "uint16_t \{name} = \{self.word(e)};"
        Wide => "int \{name} = \{self.expr(e)};"
        Flag => "bool \{name} = \{self.expr(e)};"
      }
    }
    Store(Ram, a, value) => {
      let target = self.access(Ram, a)
      let type_name = self.type_of(Load(Ram, a))
      match value {
        Binary(Add, Load(Ram, b), Const(1)) if b == a => "\{target}++;"
        Binary(Sub, Load(Ram, b), Const(1)) if b == a => "\{target}--;"
        Binary(And | Or | Xor as op, Load(Ram, b), r) if b == a && self.narrow(r) => {
          let operand = self.typed(r, type_name, mask=op == And).unwrap_or(self.expr(r))
          "\{target} \{bin_symbol(op)}= \{operand};"
        }
        _ => "\{target} = \{self.typed(value, type_name).unwrap_or(self.byte(value))};"
      }
    }
    Store(_, a, value) => "dynamic_ram_write(\{self.word(a)}, \{self.byte(value)});"
    Effect("write_joypads", args) => {
      let v = self.byte(args[0])
      "write_joypad1(\{v}); write_joypad2(\{v});"
    }
    Effect("apu_write", args) => "apu_write(\{self.word(args[0])}, \{self.byte(args[1])});"
    Effect("ppu_bus_write", args) => "ppu_bus_write(\{self.expr(args[0])}, \{self.byte(args[1])});"
    Effect(name, args) => name + "(" + args.map(fn(a) { self.byte(a) }).join(", ") + ");"
    Eval(e) => "(void)\{self.expr(e)};"
    Call(name) => "\{name}();"
  }
}

// What a block that only leaves the function does, as one line.
fn CFunc::exit_code(self : CFunc, term : @ir.Term) -> String? {
  match term {
    Return => Some("return;")
    TailCall(g) => Some("\{g}(); return;")
    Yield(pc) => Some("cpu_yield(0x\{hex(pc)}); return;")
    Trap(pc) => Some(if self.f.helper { "cpu_unresolved_jump(0x\{hex(pc)});" } else { "cpu_unresolved_jump(0x\{hex(pc)}); return;" })
    ReturnValue(e) => Some("return cpu_dispatch_target = \{self.word(e)};")
    _ => None
  }
}

priv enum Line {
  Text(Int, String)
  Mark(Int) // a block label, printed when some goto uses it
}

priv struct Structurer {
  c : CFunc
  layout : Array[Int]
  pos : Array[Int] // -1: printed inline where reached
  edges : Array[(Int, Int)]
  lines : Array[Line]
  used : Set[Int]
  mut indent : Int
}

fn Structurer::is_exit(self : Structurer, b : Int) -> Bool {
  self.pos[b] < 0
}

fn Structurer::label(self : Structurer, b : Int) -> String {
  let l = self.c.f.blocks[b].label
  if l == "" { "L\{b}" } else { l }
}

fn Structurer::emit(self : Structurer, text : String) -> Unit {
  self.lines.push(Text(self.indent, text))
}

// A transfer to block `b` from where it is not the next block.
fn Structurer::jump(self : Structurer, b : Int) -> String {
  if self.is_exit(b) {
    self.c.exit_code(self.c.f.blocks[b].term).unwrap()
  } else {
    self.used.add(b)
    "goto \{self.label(b)};"
  }
}

// No edge enters positions (s, e) from outside [s, e).
fn Structurer::single_entry(self : Structurer, s : Int, e : Int) -> Bool {
  !self.edges.iter().any(fn(edge) {
    let (from, to) = edge
    let pf = self.pos[from]
    let pt = self.pos[to]
    pt > s && pt < e && (pf < s || pf >= e)
  })
}

fn Structurer::cond(self : Structurer, e : @ir.Expr) -> String {
  self.c.expr(e)
}

fn Structurer::stmts(self : Structurer, b : Int) -> Unit {
  for s in self.c.f.blocks[b].stmts { self.emit(self.c.stmt(s)) }
}

// The latest conditional back edge to the block at position i that closes
// a single-entry region inside [i, e).
fn Structurer::find_loop(self : Structurer, i : Int, e : Int) -> Int? {
  let header = self.layout[i]
  let mut found : Int? = None
  for j in i..<e {
    match self.c.f.blocks[self.layout[j]].term {
      Branch(_, t, f) if (t == header || f == header) && t != f && self.single_entry(i, j + 1) => found = Some(j)
      _ => ()
    }
  }
  found
}

fn Structurer::range(self : Structurer, s : Int, e : Int, follow : Int, skip_loop_at : Int) -> Unit {
  let mut i = s
  while i < e {
    let b = self.layout[i]
    let next = if i + 1 < e { self.layout[i + 1] } else { follow }
    if i != skip_loop_at && self.find_loop(i, e) is Some(j) {
      let latch = self.layout[j]
      self.emit("do {")
      self.indent += 1
      self.range(i, j, latch, i)
      self.lines.push(Mark(latch))
      self.stmts(latch)
      self.indent -= 1
      let after = if j + 1 < e { self.layout[j + 1] } else { follow }
      match self.c.f.blocks[latch].term {
        Branch(c, t, f) => {
          let (cond, other) = if t == b { (c, f) } else { (@ir.simplify(Unary(Not, c)), t) }
          self.emit("} while (\{self.cond(cond)});")
          if other != after { self.emit(self.jump(other)) }
        }
        _ => ()
      }
      i = j + 1
      continue
    }
    self.lines.push(Mark(b))
    self.stmts(b)
    match self.c.f.blocks[b].term {
      Goto(t) => if t != next { self.emit(self.jump(t)) }
      Branch(c, t, f) => {
        // Branch to `other` when `when`, else continue with `next`.
        let (when, other) = if f == next {
          (c, t)
        } else if t == next {
          (@ir.simplify(Unary(Not, c)), f)
        } else {
          self.emit("if (\{self.cond(c)}) { \{self.jump(t)} }")
          self.emit(self.jump(f))
          i += 1
          continue
        }
        if self.is_exit(other) {
          self.emit("if (\{self.cond(when)}) {")
          self.indent += 1
          self.emit(self.jump(other))
          self.indent -= 1
          self.emit("}")
          i += 1
          continue
        }
        // The region skipped ends at `other`: inside this range, or at its
        // end when `other` is what follows the range.
        let k = if self.pos[other] > i && self.pos[other] < e { self.pos[other] } else if other == follow { e } else { -1 }
        if k > i + 1 && self.single_entry(i + 1, k) {
          let skip = @ir.simplify(Unary(Not, when))
          // if/else: the body ends by jumping over a second region
          let last = self.layout[k - 1]
          match self.c.f.blocks[last].term {
            Goto(m) if m != other && !self.is_exit(m) => {
              let k2 = if self.pos[m] > k && self.pos[m] < e { self.pos[m] } else if m == follow { e } else { -1 }
              if k2 > k && self.single_entry(k, k2) {
                self.emit("if (\{self.cond(skip)}) {")
                self.indent += 1
                self.range(i + 1, k, m, -1)
                self.indent -= 1
                self.emit("} else {")
                self.indent += 1
                self.range(k, k2, m, -1)
                self.indent -= 1
                self.emit("}")
                i = k2
                continue
              }
            }
            _ => ()
          }
          self.emit("if (\{self.cond(skip)}) {")
          self.indent += 1
          self.range(i + 1, k, other, -1)
          self.indent -= 1
          self.emit("}")
          i = k
          continue
        }
        self.emit("if (\{self.cond(when)}) { \{self.jump(other)} }")
      }
      Dispatch(helper, return_address, cases) => {
        self.emit("switch (\{helper}(0x\{hex(return_address)})) {")
        self.indent += 1
        for case in cases {
          self.emit("case 0x\{hex(case.0)}: \{case.1}(); return;")
        }
        self.emit("default: cpu_unresolved_jump(cpu_dispatch_target); return; // index outside the proven table")
        self.indent -= 1
        self.emit("}")
      }
      term => self.emit(self.c.exit_code(term).unwrap())
    }
    i += 1
  }
}

fn function_body(f : @ir.Function, views : Views) -> Array[Line] {
  let c : CFunc = { f, names: temp_names(f), views }
  let pos = f.blocks.map(fn(_) { -1 })
  let layout = []
  // only blocks reachable from the entry are printed
  let reachable : Set[Int] = Set([])
  let work = [f.entry]
  while work.pop() is Some(b) {
    if reachable.contains(b) { continue }
    reachable.add(b)
    for s in f.blocks[b].term.successors() { work.push(s) }
  }
  for b, block in f.blocks {
    let inline = block.stmts.is_empty() && c.exit_code(block.term) is Some(_)
    if reachable.contains(b) && !inline {
      pos[b] = layout.length()
      layout.push(b)
    }
  }
  let edges = []
  for b, block in f.blocks {
    if reachable.contains(b) {
      for s in block.term.successors() { edges.push((b, s)) }
    }
  }
  let st : Structurer = { c, layout, pos, edges, lines: [], used: Set([]), indent: 1 }
  if pos[f.entry] < 0 {
    st.emit(c.exit_code(f.blocks[f.entry].term).unwrap())
  } else {
    if f.entry != layout[0] { st.emit(st.jump(f.entry)) }
    st.range(0, layout.length(), -1, -1)
  }
  // Keep only the labels some goto uses. A label must label a statement:
  // before a declaration it gets an empty one.
  let out = []
  for k, line in st.lines {
    match line {
      Mark(b) =>
        if st.used.contains(b) {
          let declares = match st.lines.get(k + 1) {
            Some(Text(_, text)) =>
              ["uint8_t ", "uint16_t ", "int ", "bool "].iter().any(fn(t) { text.has_prefix(t) })
            _ => false
          }
          out.push(Text(0, "\n" + st.label(b) + (if declares { ":;" } else { ":" })))
        }
      text => out.push(text)
    }
  }
  out
}

fn interface_names(mask : Int) -> String {
  let names = @ir.vars_of(mask).map(fn(v) { v.interface_name() })
  if names.is_empty() { "none" } else { names.join(", ") }
}

fn Emitter::emit_function(self : Emitter, program : @ir.Program, f : @ir.Function, views : Views) -> Unit {
  let info = program.info.unwrap()
  self.indentation = 0
  if f.native {
    self.emit("// \{f.name}: native implementation, not generated.")
    self.emit("// It may read: \{interface_names(info.inputs(f.name))}.")
    self.emit("// It must produce: \{interface_names(program.outputs(info, f.name))}.\n")
    return
  }
  for line in f.doc { self.emit("// \{line}") }
  if f.helper {
    self.emit("// Inline-table dispatch proven by evaluating \{f.name}; its effects run here unchanged.")
    self.emit("static uint16_t \{f.name}(uint16_t return_address) {")
  } else {
    let inputs = info.inputs(f.name)
    let outputs = program.outputs(info, f.name)
    if inputs != 0 || outputs != 0 {
      self.emit("// in: \{interface_names(inputs)}; out: \{interface_names(outputs)}")
    }
    self.emit("void \{f.name}(void) {")
  }
  for line in function_body(f, views) {
    match line {
      Text(indent, text) => {
        self.indentation = indent
        self.emit(text)
      }
      Mark(_) => ()
    }
  }
  self.indentation = 0
  self.emit("}\n")
}

fn Emitter::emit_program_c(self : Emitter, program : @ir.Program, preamble : Array[String], views : Views) -> Unit {
  self.emit("#include \"code.h\"")
  if !views.list.is_empty() { self.emit("#include \"vars.h\"") }
  self.emit("")
  if !preamble.is_empty() {
    for line in preamble { self.emit("// \{line}") }
    self.emit("")
  }
  for f in program.functions { self.emit_function(program, f, views) }
  self.emit("void cpu_reset_entry(void) { \{program.reset}(); }")
  self.emit("void cpu_nmi_entry(void) { \{program.nmi}(); }")
}
