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
  conv : Convention
  // The function has locals (see locals.mbt): its own registers, flags and
  // scratch bytes, except those in `memory`, which it keeps in the
  // machine's state.
  locals : Bool
  memory : Int
  // With locals, what is live after each statement of each block.
  after : Array[Array[Int]]
  // With locals, its scratch values that are locals, and the statement
  // being printed (-1: the block's terminator), for which they say.
  webs : Webs?
  // The map types A, X and Y hold, by block and statement (register_types).
  mut types : Array[Array[Array[String]]]
  // The scratch bytes' names: in RAM ("" for `ram[k]`), and as locals.
  scratch_ram : Array[String]
  scratch_local : Array[String]
  // The carry chains printed as 16-bit arithmetic (words.mbt).
  mut words : Map[Int, WordFold]
  mut block : Int
  mut stmt : Int
}

// The slots as locals, which temporaries must not shadow.
let local_names : Array[String] = [
  "a", "x", "y", "carry", "zero", "negative", "overflow", "t0", "t1", "t2", "t3", "t4", "t5", "t6", "t7",
]

fn temp_names(f : @ir.Function, locals~ : Bool = false) -> Array[String] {
  let seen : Map[String, Int] = Map([])
  f.temps.map(fn(t) {
    // the bit a rotate shifts out, `carry_out` beside the local `carry`
    let hint = if locals && local_names.contains(t.hint) { t.hint + "_out" } else { t.hint }
    let n = seen.get(hint).unwrap_or(0) + 1
    seen[hint] = n
    if n == 1 { hint } else { hint + n.to_string() }
  })
}

// A slot as this function names it: its local, or its place in the
// machine's state.
fn CFunc::slot(self : CFunc, s : Slot) -> String {
  if (self.memory & s.bit()) != 0 { s.machine_name() } else { s.local_name() }
}

fn CFunc::reg(self : CFunc, v : @ir.Var) -> String {
  self.slot(Reg(v))
}

// The scratch byte an address is, when the value read there is a local.
fn CFunc::local_scratch(self : CFunc, a : @ir.Expr) -> Int? {
  guard self.webs is Some(webs) && scratch_of(a) is Some(k) else { return None }
  let bits = if self.stmt < 0 { webs.terms[self.block] } else { webs.uses[self.block][self.stmt] }
  if (bits & (1 << k)) != 0 { Some(k) } else { None }
}

// The same for the value the statement writes.
fn CFunc::local_scratch_write(self : CFunc, a : @ir.Expr) -> Int? {
  guard self.webs is Some(webs) && scratch_of(a) is Some(k) && self.stmt >= 0 else { return None }
  if (webs.defs[self.block][self.stmt] & (1 << k)) != 0 { Some(k) } else { None }
}

fn CFunc::at(self : CFunc, block : Int, stmt : Int) -> Unit {
  self.block = block
  self.stmt = stmt
}

// `(uint8_t)` is needed unless the value is evidently a byte (clang's
// -Wconversion reasoning: loads, byte variables, masks, comparisons).
fn CFunc::narrow(self : CFunc, e : @ir.Expr) -> Bool {
  match e {
    Const(v) | Sym(_, v) => v >= 0 && v <= 255
    Var(_) => true
    Runtime(name) => @ir.runtime_width(name) == U8
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
    Param | Runtime(_) => true
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
  // (the runtime's helpers read the machine's X and Y; with locals, the
  // pointer is read and the index added here)
  // (and a pointer in scratch bytes that are locals is printed as such)
  let in_memory = fn(p : @ir.Expr) {
    match p.value() {
      Some(v) => self.local_scratch(Const(v)) is None && self.local_scratch(Const(v + 1)) is None
      None => true
    }
  }
  match e {
    Binary(Add, base, Var(Y)) =>
      match pointer(base) {
        Some(p) if !in_memory(p) => None
        Some(p) if p.is_atom() && self.locals => Some("(uint16_t)(zero_page_pointer(\{self.expr(p)}) + y)")
        Some(p) if p.is_atom() => Some("indirect_y_addr(\{self.expr(p)})")
        _ => None
      }
    _ =>
      match pointer(e) {
        Some(Unary(Byte, Binary(Add, p, Var(X)))) if p.is_atom() && self.locals =>
          Some("zero_page_pointer((uint8_t)(\{self.paren(p, 4)} + x))")
        Some(Unary(Byte, Binary(Add, p, Var(X)))) if p.is_atom() => Some("indirect_x_addr(\{self.expr(p)})")
        _ => None
      }
  }
}

// A memory access by the name of what it reads or writes, or by address.
// The scratch bytes this function has as locals, by bit.
fn CFunc::local_scratch_mask(self : CFunc) -> Int {
  match self.webs {
    Some(webs) => webs.locals
    None => 0
  }
}

// A computed address, checked against those with `--check-scratch`.
fn CFunc::guarded(self : CFunc, address : String) -> String {
  let mask = self.local_scratch_mask()
  if self.conv.check && mask != 0 { "scratch_guard(\{address}, 0x\{hex(mask)})" } else { address }
}

// A computed RAM address with what --locals takes of it checked
// (`--check-scratch`).
fn CFunc::checked_ram(self : CFunc, a : @ir.Expr) -> String {
  let index = match a {
    Unary(Byte, inner) => "page_guard(\{self.expr(inner)})"
    Binary(And, inner, Const(0x7ff)) => "ram_end_guard(\{self.expr(inner)})"
    _ if constant_part(a) >= 0 && constant_part(a) < scratch_bytes => "scratch_index_guard(\{self.expr(a)})"
    _ => self.expr(a)
  }
  "ram[\{self.guarded(index)}]"
}

// An index into an array or a record: its terms and constant offset.
fn CFunc::index_text(self : CFunc, offset : Int, index : Array[@ir.Expr]) -> String {
  let terms = index.map(fn(e) { self.paren(e, 4) }).join(" + ")
  if index.is_empty() {
    show_number(offset)
  } else if offset > 0 {
    "\{show_number(offset)} + \{terms}"
  } else if offset < 0 {
    "\{terms} - \{show_number(-offset)}"
  } else {
    terms
  }
}

fn CFunc::access(self : CFunc, space : @ir.Mem, a : @ir.Expr) -> String {
  if space == Ram && self.local_scratch(a) is Some(k) { return self.scratch_local[k] }
  if space == Ram && scratch_of(a) is Some(k) && self.scratch_ram[k] != "" { return self.scratch_ram[k] }
  if space == Ram && self.conv.check && a.value() is None && !(a is Binary(Add, Const(0x100), Var(SP))) {
    return self.checked_ram(a)
  }
  if space == Ram && self.views.record_place(a) is Some((r, field, offset, index)) {
    return "\{r.name}(\{self.index_text(offset, index)}).\{field}"
  }
  match self.views.place(a) {
    Some({ view, offset, index }) if view.kind.names(space) => {
      if view.kind == Scalar || view.kind == WramScalar { return view.name }
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
      match (a, space) {
        (Binary(Add, Const(0x100), i), Ram) if self.views.stack => "stack[\{self.expr(i)}]"
        (_, Rom) => "ROM(\{self.expr(a)})"
        (_, Bank(bank)) => "BANK_ROM(\{bank}, \{self.expr(a)})"
        (_, Window) => "WINDOW_ROM(\{self.expr(a)})"
        (_, Wram) => "WRAM(\{self.expr(a)})"
        _ => "ram[\{self.expr(a)}]"
      }
  }
}

// The map type of a value: a typed variable's, kept by masking and
// combining it with others of its type.
fn CFunc::type_of(self : CFunc, e : @ir.Expr) -> String {
  let env = match self.types.get(self.block) {
    Some(by_stmt) => by_stmt[if self.stmt < 0 { by_stmt.length() - 1 } else { self.stmt }]
    None => ["", "", ""]
  }
  self.type_with(e, env)
}

// The same with the types A, X and Y hold (a register loaded from a typed
// variable holds its type until written).
fn CFunc::type_with(self : CFunc, e : @ir.Expr, env : Array[String]) -> String {
  match e {
    Var(A) => env[0]
    Var(X) => env[1]
    Var(Y) => env[2]
    Load(Ram, a) =>
      match (self.views.record_place(a), self.views.place(a)) {
        (Some((r, field, _, _)), _) => r.type_of(field)
        (_, Some({ view, .. })) => view.type_name
        _ => ""
      }
    Binary(And | Or | Xor, l, r) => {
      let t = self.type_with(l, env)
      if t != "" { t } else { self.type_with(r, env) }
    }
    _ => ""
  }
}

fn register_slot(v : @ir.Var) -> Int {
  match v {
    A => 0
    X => 1
    Y => 2
    _ => -1
  }
}

// The types A, X and Y hold before each statement of each block (and,
// last, before its terminator): a forward pass over the blocks.
fn CFunc::register_types(self : CFunc) -> Array[Array[Array[String]]] {
  let f = self.f
  let out : Array[Array[Array[String]]] = f.blocks.map(fn(_) { [] })
  let ins : Array[Array[String]?] = f.blocks.map(fn(_) { None })
  ins[f.entry] = Some(["", "", ""])
  let mut changed = true
  while changed {
    changed = false
    for b, block in f.blocks {
      guard ins[b] is Some(start) else { continue }
      let env = start.copy()
      let rows = []
      for s in block.stmts {
        rows.push(env.copy())
        match s {
          Assign(v, e) if register_slot(v) >= 0 => env[register_slot(v)] = self.type_with(e, env)
          Call(g) | CallValue(_, g, _) => {
            let outputs = self.conv.sig(g).outputs
            for v in [@ir.Var::A, X, Y] {
              if (outputs & v.bit()) != 0 || !self.conv.locals { env[register_slot(v)] = "" }
            }
          }
          _ => ()
        }
      }
      rows.push(env.copy())
      out[b] = rows
      for t in block.term.successors() {
        let next = match ins[t] {
          None => env.copy()
          Some(old) => old.mapi(fn(i, ty) { if ty == env[i] { ty } else { "" } })
        }
        if ins[t] != Some(next) {
          ins[t] = Some(next)
          changed = true
        }
      }
    }
  }
  out
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
    Var(v) => (self.reg(v), 0)
    Temp(t) => (self.names[t], 0)
    Param => ("return_address", 0)
    Runtime(name) => (name, 0)
    Load(Bus, a) => ("read_byte(\{self.guarded(self.word(a))})", 0)
    Load(space, a) => (self.access(space, a), 0)
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
  let name = self.reg(v)
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

// `live`: the registers and flags live after the statement.
fn CFunc::stmt(self : CFunc, s : @ir.Stmt, live~ : Int = local_slots) -> String {
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
      let write = self.local_scratch_write(a)
      let target = match (write, scratch_of(a)) {
        (Some(k), _) => self.scratch_local[k]
        // the byte in RAM, though this statement may read a local of it
        (None, Some(k)) => if self.scratch_ram[k] != "" { self.scratch_ram[k] } else { "ram[\{k}]" }
        _ => self.access(Ram, a)
      }
      let type_name = self.type_of(Load(Ram, a))
      // (a scratch byte read in RAM and written to a local, or the other
      // way, is no update in place)
      let same = scratch_of(a) is None || write == self.local_scratch(a)
      match value {
        Binary(Add, Load(Ram, b), Const(1)) if b == a && same => "\{target}++;"
        Binary(Sub, Load(Ram, b), Const(1)) if b == a && same => "\{target}--;"
        Binary(And | Or | Xor as op, Load(Ram, b), r) if b == a && same && self.narrow(r) => {
          let operand = self.typed(r, type_name, mask=op == And).unwrap_or(self.expr(r))
          "\{target} \{bin_symbol(op)}= \{operand};"
        }
        _ => "\{target} = \{self.typed(value, type_name).unwrap_or(self.byte(value))};"
      }
    }
    Store(Wram, a, value) => "\{self.access(Wram, a)} = \{self.byte(value)};"
    Store(_, a, value) => {
      let address = if self.conv.check && self.locals && a.value() is None {
        "pointer_guard(\{self.word(a)})"
      } else {
        self.guarded(self.word(a))
      }
      "dynamic_ram_write(\{address}, \{self.byte(value)});"
    }
    Effect("write_joypads", args) => {
      let v = self.byte(args[0])
      "write_joypad1(\{v}); write_joypad2(\{v});"
    }
    Effect("apu_write" | "mapper_write" as name, args) => "\{name}(\{self.word(args[0])}, \{self.byte(args[1])});"
    Effect("cpu_await", args) => "cpu_await(\{self.word(args[0])});"
    Effect("ppu_bus_write", args) => "ppu_bus_write(\{self.expr(args[0])}, \{self.byte(args[1])});"
    Effect(name, args) => name + "(" + args.map(fn(a) { self.byte(a) }).join(", ") + ");"
    Eval(e) => "(void)\{self.expr(e)};"
    Call(name) => self.call(name, live~)
    CallValue(t, helper, return_address) => "uint16_t \{self.names[t]} = \{self.helper_call(helper, return_address)};"
  }
}

// The arguments of a call of a routine with locals: its parameters by
// value, then the results it writes back.
fn CFunc::arguments(self : CFunc, g : String) -> Array[String] {
  let s = self.conv.sig(g)
  let args = []
  for slot in slots_of(s.value_bits()) { args.push(self.slot(slot)) }
  for slot in slots_of(s.pointer_bits()) { args.push("&" + self.slot(slot)) }
  args
}

// A call of `g` and the result it returns, if any.
fn CFunc::call_parts(self : CFunc, g : String) -> (String, Slot?) {
  if !self.conv.has_locals(g) { return ("\{g}()", None) }
  ("\{g}(\{self.arguments(g).join(", ")})", self.conv.sig(g).primary)
}

// A call as statements, its results assigned: with locals, those `live`
// afterwards (the machine's state takes every result). The slots the
// callee keeps in the machine's state and this function has as locals go
// through the machine's state.
fn CFunc::call(self : CFunc, g : String, live~ : Int = local_slots) -> String {
  let live = if self.locals { live } else { local_slots }
  let s = self.conv.sig(g)
  let through = self.conv.memory(g) & local_slots & self.memory.lnot()
  let parts = []
  for slot in slots_of(s.params & through) { parts.push("\{slot.machine_place()} = \{slot.local_name()};") }
  let (call, primary) = self.call_parts(g)
  // (X back as it was, by the analysis: no need to assign it)
  let unchanged = self.locals && self.stmt >= 0 &&
    self.conv.same_x.get(self.f.name).map(fn(set) { set.contains((self.block << 16) | self.stmt) }).unwrap_or(false)
  parts.push(match primary {
    Some(Reg(X)) if unchanged => call + ";"
    Some(p) if (live & p.bit()) != 0 => "\{self.slot(p)} = \{call};"
    _ => call + ";"
  })
  for slot in slots_of(s.outputs & through & live) { parts.push("\{slot.local_name()} = \{slot.machine_place()};") }
  parts.join(" ")
}

fn CFunc::helper_call(self : CFunc, helper : String, return_address : Int) -> String {
  let args = ["0x\{hex(return_address)}"]
  if self.conv.has_locals(helper) { args.append(self.arguments(helper)) }
  "\{helper}(\{args.join(", ")})"
}

// Whether the function returns a value (a result, or a helper's target).
fn CFunc::returns_value(self : CFunc) -> Bool {
  self.f.helper || (self.locals && self.conv.sig(self.f.name).primary is Some(_))
}

// Writing back the results passed by pointer, before a return.
fn CFunc::write_back(self : CFunc) -> String {
  if !self.locals { return "" }
  let s = self.conv.sig(self.f.name)
  slots_of(s.pointer_bits()).map(fn(slot) { "*\{pointer_name(s, slot)} = \{slot.local_name()}; " }).join("")
}

fn CFunc::return_code(self : CFunc) -> String {
  let value = match self.conv.sig(self.f.name).primary {
    Some(p) if self.locals => " " + p.local_name()
    _ => ""
  }
  self.write_back() + "return\{value};"
}

// A call of `g` in tail position: its results, then the routine's.
fn CFunc::tail_call(self : CFunc, g : String) -> String {
  if self.locals && self.conv.has_locals(g) {
    let own = self.conv.sig(self.f.name)
    let s = self.conv.sig(g)
    // the callee's results are the routine's
    let kept = (own.outputs | s.outputs) & (own.memory | s.memory)
    if own.outputs == s.outputs && kept == 0 && own.pointer_bits() == 0 && s.pointer_bits() == 0 {
      let (call, primary) = self.call_parts(g)
      return if primary is Some(_) { "return \{call};" } else { "\{call}; return;" }
    }
  }
  self.call(g, live=self.conv.sig(self.f.name).outputs) + " " + self.return_code()
}

// After an unproven transfer, which never returns: `return;` keeps the
// shape of the other exits, except where a value would be due.
fn CFunc::after_trap(self : CFunc) -> String {
  if self.returns_value() { "" } else { " return;" }
}

// What a block that only leaves the function does, as one line.
fn CFunc::exit_code(self : CFunc, term : @ir.Term) -> String? {
  match term {
    Return => Some(self.return_code())
    TailCall(g) => Some(self.tail_call(g))
    Yield(pc) => Some("cpu_yield(0x\{hex(pc & 0xffff)}); return;")
    Trap(pc) => Some("cpu_unresolved_jump(0x\{hex(pc & 0xffff)});\{self.after_trap()}")
    ReturnValue(e) => Some(self.write_back() + "return cpu_dispatch_target = \{self.word(e)};")
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
    self.c.at(b, -1)
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

// Whether C text mentions an identifier.
fn mentions(text : String, name : String) -> Bool {
  let ident = fn(u : UInt16) { let c = u.to_int(); (c >= 48 && c <= 57) || (c >= 65 && c <= 90) || (c >= 97 && c <= 122) || c == 95 }
  let mut from = 0
  while text[from:].find(name) is Some(k) {
    let start = from + k
    let end = start + name.length()
    let before = start == 0 || !ident(text[start - 1])
    let after = end >= text.length() || !ident(text[end])
    if before && after { return true }
    from = start + 1
  }
  false
}

fn Structurer::cond(self : Structurer, e : @ir.Expr) -> String {
  self.c.expr(e)
}

fn Structurer::stmts(self : Structurer, b : Int) -> Unit {
  self.c.at(b, -1)
  for k, s in self.c.f.blocks[b].stmts {
    self.c.at(b, k)
    let live = if self.c.locals { self.c.after[b][k] } else { local_slots }
    if self.c.locals && dead(s, live, self.c.memory) { continue }
    // a write to a scratch local that nothing reads
    match (self.c.webs, s) {
      (Some(webs), Store(Ram, _, value)) if webs.dead[b][k] != 0 && value.is_pure() => continue
      _ => ()
    }
    match self.c.word_stmt(self.c.words, b, k, s, live) {
      Some("") => continue
      Some(text) => { self.emit(text); continue }
      None => ()
    }
    self.emit(self.c.stmt(s, live~))
  }
  self.c.at(b, -1)
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
      let first = self.lines.length()
      self.stmts(latch)
      // the temporaries the latch declares, out of scope in the condition
      let declared = []
      for line in self.lines[first:] {
        guard line is Text(_, text) else { continue }
        for t in ["uint8_t ", "uint16_t ", "int ", "bool "] {
          if text.has_prefix(t) {
            let rest = text[t.length():].to_owned()
            match rest.find(" ") { Some(k) => declared.push(rest[:k].to_owned()); None => () }
          }
        }
      }
      let after = if j + 1 < e { self.layout[j + 1] } else { follow }
      match self.c.f.blocks[latch].term {
        Branch(c, t, f) => {
          let (cond, other) = if t == b { (c, f) } else { (@ir.simplify(Unary(Not, c)), t) }
          let text = self.cond(cond)
          if declared.iter().any(fn(name) { mentions(text, name) }) {
            self.emit("if (!(\{text})) break;")
            self.indent -= 1
            self.emit("} while (1);")
          } else {
            self.indent -= 1
            self.emit("} while (\{text});")
          }
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
        self.emit("switch (\{self.c.helper_call(helper, return_address)}) {")
        self.indent += 1
        for case in cases {
          self.emit("case 0x\{hex(case.0)}: \{self.c.tail_call(case.1)}")
        }
        self.emit("default: cpu_unresolved_jump(cpu_dispatch_target);\{self.c.after_trap()} // index outside the proven table")
        self.indent -= 1
        self.emit("}")
      }
      Jump(target, cases) => {
        let value = self.c.word(target)
        self.emit("switch (\{value}) {")
        self.indent += 1
        for case in cases {
          self.emit("case 0x\{hex(case.0)}: \{self.c.tail_call(case.1)}")
        }
        self.emit("default: cpu_unresolved_jump(\{value});\{self.c.after_trap()} // not a code address of the map")
        self.indent -= 1
        self.emit("}")
      }
      Switch(value, cases, other) => {
        self.emit("switch (\{self.c.expr(value)}) {")
        self.indent += 1
        for case in cases {
          self.emit("case \{show_number(case.0)}: \{self.jump(case.1)}")
        }
        self.emit("default: \{self.jump(other)}")
        self.indent -= 1
        self.emit("}")
      }
      term => self.emit(self.c.exit_code(term).unwrap())
    }
    i += 1
  }
}

fn function_body(f : @ir.Function, views : Views, conv : Convention, plan~ : ScratchPlan = ScratchPlan::plain()) -> Array[Line] {
  let locals = conv.has_locals(f.name)
  let after = if locals { conv.live_after(f) } else { [] }
  let c : CFunc = {
    f,
    names: temp_names(f, locals~),
    views,
    conv,
    locals,
    memory: conv.memory(f.name),
    after,
    webs: if locals { conv.webs.get(f.name) } else { None },
    types: [],
    scratch_ram: plan.ram,
    scratch_local: plan.locals,
    words: Map([]),
    block: 0,
    stmt: -1,
  }
  c.types = c.register_types()
  c.words = c.word_folds()
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
    c.at(f.entry, -1)
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
          // (or before the end of a block)
          let declares = match st.lines.get(k + 1) {
            Some(Text(_, text)) =>
              ["uint8_t ", "uint16_t ", "int ", "bool ", "}"].iter().any(fn(t) { text.has_prefix(t) })
            _ => true
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

// The same with the scratch bytes, `t0` to `t7`.
fn slot_names(mask : Int) -> String {
  let names = slots_of(mask).map(fn(s) { s.local_name() })
  if names.is_empty() { "none" } else { names.join(", ") }
}

// What a routine calls its scratch bytes (the map's `scratch`): a name is
// the local's when the routine has no value of the byte in RAM, else the
// byte's in RAM (`#define NAME ram[K]` around the function).
priv struct ScratchPlan {
  ram : Array[String] // "" for ram[k]
  locals : Array[String] // tK by default
  defines : Array[(Int, String, String)] // byte, name, description
}

fn ScratchPlan::plain() -> ScratchPlan {
  { ram: Array::make(8, ""), locals: Array::makei(8, fn(k) { "t\{k}" }), defines: [] }
}

fn reads_scratch(e : @ir.Expr, k : Int) -> Bool {
  match e {
    Load(Ram, a) => scratch_of(a) == Some(k) || reads_scratch(a, k)
    Load(_, a) | Unary(_, a) => reads_scratch(a, k)
    Binary(_, l, r) => reads_scratch(l, k) || reads_scratch(r, k)
    _ => false
  }
}

fn scratch_plan(f : @ir.Function, views : Views, conv : Convention) -> ScratchPlan {
  let plan = ScratchPlan::plain()
  guard views.scratch.get(f.name) is Some(names) else { return plan }
  let webs = if conv.has_locals(f.name) { conv.webs.get(f.name) } else { None }
  for entry in names {
    let (k, name, description) = entry
    let bit = 1 << k
    // whether a value of the byte is in RAM somewhere in the routine
    let mut in_ram = false
    for b, block in f.blocks {
      for i, s in block.stmts {
        let reads = s.exprs().iter().any(fn(e) { reads_scratch(e, k) })
        let writes = s is Store(Ram, a, _) && scratch_of(a) == Some(k)
        let (uses, defs) = match webs {
          Some(w) => (w.uses[b][i], w.defs[b][i])
          None => (0, 0)
        }
        if (reads && (uses & bit) == 0) || (writes && (defs & bit) == 0) { in_ram = true }
      }
      let term_reads = block.term.exprs().iter().any(fn(e) { reads_scratch(e, k) })
      let term_local = match webs {
        Some(w) => (w.terms[b] & bit) != 0
        None => false
      }
      if term_reads && !term_local { in_ram = true }
    }
    if in_ram {
      plan.ram[k] = name
      plan.defines.push((k, name, description))
    } else {
      plan.locals[k] = name
    }
  }
  plan
}

// The registers and flags a printed body names (outside comments): the
// locals it declares, except its parameters by value.
fn named_locals(body : Array[Line], plan : ScratchPlan) -> Int {
  let mut named = 0
  for line in body {
    guard line is Text(_, text) else { continue }
    let code = match text.find("//") {
      Some(i) => text.view(end_offset=i).to_owned()
      None => text
    }
    for s in slots_of(local_slots) {
      let name = match s {
        Scratch(k) => plan.locals[k]
        _ => s.local_name()
      }
      if mentions(code, name) { named = named | s.bit() }
    }
  }
  named
}

// The locals a function with registers as locals declares: by type, those
// written back through a pointer starting from the value it points to.
fn local_declarations(conv : Convention, f : @ir.Function, named : Int, plan : ScratchPlan) -> Array[String] {
  let name_of = fn(s : Slot) {
    match s {
      Scratch(k) => plan.locals[k]
      _ => s.local_name()
    }
  }
  let sig = conv.sig(f.name)
  // (scratch bytes are in RAM at the routine's edges; its values in them
  // that never leave it are locals)
  let declared = (named & sig.value_bits().lnot() & sig.memory.lnot()) | (named & all_scratch)
  let out = []
  // (a result written back through a pointer starts from what it points to)
  let from_pointer = declared & sig.params & sig.pointer_bits()
  for type_name in ["uint8_t", "bool"] {
    let plain = slots_of(declared & from_pointer.lnot()).filter(fn(s) { s.c_type() == type_name })
    if !plain.is_empty() { out.push("\{type_name} \{plain.map(name_of).join(", ")};") }
  }
  for s in slots_of(from_pointer) {
    out.push("\{s.c_type()} \{s.local_name()} = *\{pointer_name(sig, s)};")
  }
  out
}

fn Emitter::emit_function(self : Emitter, program : @ir.Program, f : @ir.Function, views : Views, conv : Convention) -> Unit {
  let info = program.info.unwrap()
  self.indentation = 0
  if f.native {
    self.emit("// \{f.name}: native implementation, not generated.")
    self.emit("// It may read: \{interface_names(info.inputs(f.name))}.")
    self.emit("// It must produce: \{interface_names(program.outputs(info, f.name))}.\n")
    return
  }
  for line in f.doc { self.emit("// \{line}") }
  let plan = scratch_plan(f, views, conv)
  for d in plan.defines {
    let about = if d.2 == "" { "" } else { " // " + d.2 }
    self.emit("#define \{d.1} ram[\{d.0}]\{about}")
  }
  let locals = conv.has_locals(f.name)
  if f.helper {
    self.emit("// Inline-table dispatch proven by evaluating \{f.name}; its effects run here unchanged.")
    let params = if locals { conv.parameters(f) } else { "uint16_t return_address" }
    self.emit("static uint16_t \{f.name}(\{params}) {")
  } else if locals {
    // the prototype is the interface
    self.emit(conv.prototype(f) + " {")
  } else {
    let inputs = info.inputs(f.name)
    let outputs = program.outputs(info, f.name)
    if inputs != 0 || outputs != 0 {
      self.emit("// in: \{interface_names(inputs)}; out: \{interface_names(outputs)}")
    }
    self.emit("void \{f.name}(void) {")
  }
  let body = function_body(f, views, conv, plan~)
  if locals {
    self.indentation = 1
    for line in local_declarations(conv, f, named_locals(body, plan), plan) { self.emit(line) }
  }
  for line in body {
    match line {
      Text(indent, text) => {
        self.indentation = indent
        self.emit(text)
      }
      Mark(_) => ()
    }
  }
  self.indentation = 0
  self.emit("}")
  for d in plan.defines { self.emit("#undef \{d.1}") }
  self.emit("")
}

// Whether a routine with locals calls one that keeps the machine's
// registers (through `machine`).
fn calls_machine(program : @ir.Program, conv : Convention) -> Bool {
  program.functions.iter().any(fn(f) {
    if f.native || !conv.has_locals(f.name) { return false }
    let machine = fn(g : String) { !conv.has_locals(g) }
    f.blocks.iter().any(fn(block) {
      block.stmts.iter().any(fn(s) { s.callee().map(machine).unwrap_or(false) }) ||
      block.term.tail_calls().iter().any(machine)
    })
  })
}

fn Emitter::emit_program_c(self : Emitter, program : @ir.Program, preamble : Array[String], views : Views, conv : Convention) -> Unit {
  self.emit("#include \"code.h\"")
  if !views.list.is_empty() { self.emit("#include \"vars.h\"") }
  self.emit("")
  if !preamble.is_empty() {
    for line in preamble { self.emit("// \{line}") }
    self.emit("")
  }
  if conv.locals {
    self.emit("// Registers, flags and the scratch bytes $00-$07 (t0 to t7) are each")
    self.emit("// routine's own: its parameters, results and locals. The hand-written")
    self.emit("// routines, the routines they call and the roots keep the machine's, which")
    self.emit("// routines with locals reach through `machine` and `ram`.")
    if conv.check { self.emit("bool guard_failed; // see code.h") }
    if calls_machine(program, conv) {
      self.emit("static const struct {")
      self.emit("  uint8_t *a, *x, *y;")
      self.emit("  bool *carry, *zero, *negative, *overflow;")
      self.emit("} machine = { &a, &x, &y, &carry_flag, &zero_flag, &neg_flag, &overflow_flag };")
    }
    self.emit("")
  }
  for f in program.functions { self.emit_function(program, f, views, conv) }
  self.emit("void cpu_reset_entry(void) { \{program.reset}(); }")
  self.emit("void cpu_nmi_entry(void) { \{program.nmi}(); }")
  // the runtime of cartridges runs the IRQ handler when the mapper raises
  // it (MMC3)
  match program.irq {
    Some(irq) => self.emit("void cpu_irq_entry(void) { \{irq}(); }")
    None => self.emit("void cpu_irq_entry(void) { cpu_unresolved_jump(0xfffe); } // never enabled")
  }
}

// With locals: each generated routine as a function of the machine's
// registers (machine.c), for the routine-level differential test
// (tests/check_routines.py), which runs routines from machine states. The
// adapter takes the routine's parameters from the machine's registers, then
// overwrites those before the call, so that a routine reading them instead
// of its own fails the test, and writes the results back.
fn Emitter::emit_machine_adapters(self : Emitter, program : @ir.Program, conv : Convention) -> Unit {
  self.emit("// Each generated routine with the machine's registers, flags and scratch")
  self.emit("// bytes: its parameters taken from them and its results written back")
  self.emit("// (for tests/check_routines.py). Not part of the game.")
  self.emit("#include \"code.h\"")
  self.emit("")
  self.emit("// What a routine with locals must not read: the machine's registers once")
  self.emit("// its parameters are taken from them. (The scratch bytes stay: the test's")
  self.emit("// random indices reach them, as the game's never do.)")
  self.emit("static void poison(void) {")
  self.emit("  a = 0x5a; x = 0xa5; y = 0x3c;")
  self.emit("  carry_flag = !carry_flag; zero_flag = !zero_flag; neg_flag = !neg_flag; overflow_flag = !overflow_flag;")
  self.emit("}")
  self.emit("")
  for f in program.functions {
    if f.native || f.helper { continue }
    let sig = conv.sig(f.name)
    // what every call enters it with equal, which the test's states take on
    let entry = conv.entered.get(f.name).unwrap_or([])
    if !entry.is_empty() {
      // (registers equal to one byte are equal too)
      let first : Map[Int, Int] = Map([])
      let parts = []
      for p in entry {
        match first.get(p.1) {
          Some(r) => parts.push("\{held_vars[p.0].c_name()} = \{held_vars[r].c_name()};")
          None => {
            first[p.1] = p.0
            parts.push("ram[0x\{hex(p.1)}] = \{held_vars[p.0].c_name()};")
          }
        }
      }
      self.emit("void enter_\{f.name}(void) { \{parts.join(" ")} }")
    }
    // (what it gives back as it was is checked like a result)
    self.emit("// in: \{slot_names(sig.params)}; out: \{slot_names(sig.outputs | sig.kept)}")
    if !conv.has_locals(f.name) {
      self.emit("void machine_\{f.name}(void) { \{f.name}(); }")
      continue
    }

    let own = local_slots & sig.memory.lnot()
    let copy = fn(s : Slot) { "v_" + s.local_name() }
    let lines = []
    for s in slots_of((sig.params | sig.outputs | sig.kept) & own) { lines.push("\{s.c_type()} \{copy(s)} = \{s.machine_name()};") }
    lines.push("poison();")
    let args = slots_of(sig.value_bits()).map(copy) + slots_of(sig.pointer_bits()).map(fn(s) { "&" + copy(s) })
    let call = "\{f.name}(\{args.join(", ")});"
    lines.push(match sig.primary { Some(p) => "\{copy(p)} = \{call}"; None => call })
    for s in slots_of((sig.outputs | sig.kept) & own) { lines.push("\{s.machine_name()} = \{copy(s)};") }
    self.emit("void machine_\{f.name}(void) {")
    for line in lines { self.emit("  " + line) }
    self.emit("}")
  }
}
