// Carry chains printed as 16-bit arithmetic.
//
// The 6502 adds and subtracts 16-bit values a byte at a time, the carry of
// the low byte going into the high byte:
//
//   uint16_t sum = (uint16_t)(obj(x).x + pixel_speed + carry);
//   obj(x).x = (uint8_t)sum;
//   a = (uint8_t)(obj(x).page + page_carry + (sum >= 0x100));
//
// With locals, the low byte's sum takes the high bytes too, and the high
// byte is the sum's:
//
//   uint16_t sum = (uint16_t)(WORD(obj(x).page, obj(x).x) + WORD(page_carry, pixel_speed) + carry);
//   obj(x).x = (uint8_t)sum;
//   a = (uint8_t)(sum >> 8);
//
// This is exact when (1) the low sum T, as an integer, has a carry of 0 or
// 1 (an addition: 0 <= T <= 511) or a borrow of 0 or 1 (a subtraction:
// -256 <= T <= 255), so the test `T >= 0x100` (or `T < 0`) is floor(T /
// 256); (2) the sum is otherwise used only for its low byte; and (3)
// nothing between the two statements writes what the high operands read,
// as they are now read with the low ones. Then the high byte, (H + floor(T
// / 256)) mod 256, is that of 256 H + T. A comparison of two bytes, then
// a subtraction with the borrow that keeps only the carry, is a 16-bit
// comparison:
//
//   carry = obj(x).x >= middle_x;
//   carry = (obj(x).page - middle_page - !carry) >= 0;
//
// is `carry = WORD(obj(x).page, obj(x).x) >= WORD(middle_page, middle_x);`.

priv enum WordFold {
  // The low sum: its terms (negated?, term, the high term paired with it),
  // the high terms paired with none, and where the high byte is taken.
  Low(Array[(Bool, @ir.Expr, @ir.Expr?)], Array[(Bool, @ir.Expr)], Int)
  // The high byte, from the sum in this temporary.
  High(Int)
  // A comparison folded into the next statement: printed there.
  Folded
  // The 16-bit comparison: high and low bytes of both sides, and where the
  // low bytes are compared.
  Compare(@ir.Expr, @ir.Expr, @ir.Expr, @ir.Expr, Int)
}

fn word_key(b : Int, k : Int) -> Int {
  b * 65536 + k
}

// The terms of a sum or difference, with their signs.
fn signed_terms(e : @ir.Expr, negated : Bool, out : Array[(Bool, @ir.Expr)]) -> Unit {
  match e {
    Binary(Add, l, r) => { signed_terms(l, negated, out); signed_terms(r, negated, out) }
    Binary(Sub, l, r) => { signed_terms(l, negated, out); signed_terms(r, !negated, out) }
    _ => out.push((negated, e))
  }
}

// The value's bounds, if it is a byte, a flag or a constant.
fn CFunc::term_bounds(self : CFunc, e : @ir.Expr) -> (Int, Int)? {
  match e.value() {
    Some(v) => return if v >= 0 && v <= 255 { Some((v, v)) } else { None }
    None => ()
  }
  if e.is_boolean() { Some((0, 1)) } else if self.narrow(e) { Some((0, 255)) } else { None }
}

// Occurrences of the temporary that keep only its low byte.
fn low_uses(e : @ir.Expr, t : Int) -> Int {
  match e {
    Unary(Byte | Lo, Temp(u)) if u == t => 1
    Load(_, a) | Unary(_, a) => low_uses(a, t)
    Binary(_, l, r) => low_uses(l, t) + low_uses(r, t)
    _ => 0
  }
}

fn term_exprs(term : @ir.Term) -> Array[@ir.Expr] {
  match term {
    Branch(e, _, _) | ReturnValue(e) | Jump(e, _) | Switch(e, _, _) => [e]
    _ => []
  }
}

// Whether every use of the temporary but `allowed` keeps only its low byte.
fn only_low_uses(f : @ir.Function, t : Int, allowed : Int) -> Bool {
  let mut all = 0
  let mut low = 0
  for block in f.blocks {
    for s in block.stmts {
      match s {
        Assign(v, Temp(u)) if u == t && !v.is_flag() => { all += 1; low += 1 }
        Store(_, a, Temp(u)) if u == t => { all += 1 + a.uses_temp(t); low += 1 + low_uses(a, t) }
        Let(t2, Temp(u)) if u == t && f.temps[t2].width == U8 => { all += 1; low += 1 }
        Assign(_, e) | Let(_, e) | Eval(e) => { all += e.uses_temp(t); low += low_uses(e, t) }
        Store(_, a, e) => { all += a.uses_temp(t) + e.uses_temp(t); low += low_uses(a, t) + low_uses(e, t) }
        Effect(_, args) => for e in args { all += e.uses_temp(t); low += low_uses(e, t) }
        Call(_) | CallValue(_, _, _) => ()
      }
    }
    for e in term_exprs(block.term) { all += e.uses_temp(t); low += low_uses(e, t) }
  }
  all == low + allowed
}

// The RAM addresses `e` reads.
fn ram_loads(e : @ir.Expr, out : Array[@ir.Expr]) -> Unit {
  match e {
    Load(Ram, a) => { out.push(a); ram_loads(a, out) }
    Load(_, a) | Unary(_, a) => ram_loads(a, out)
    Binary(_, l, r) => { ram_loads(l, out); ram_loads(r, out) }
    _ => ()
  }
}

fn reads_other_memory(e : @ir.Expr) -> Bool {
  match e {
    Load(Ram | Rom, a) | Unary(_, a) => reads_other_memory(a)
    Load(_, _) | Runtime(_) => true
    Binary(_, l, r) => reads_other_memory(l) || reads_other_memory(r)
    _ => false
  }
}

// A constant, what is added to it, and the bytes the sum wraps at: 2 KiB
// for RAM (it repeats), 256 for zero-page indexing (`(uint8_t)(base + x)`).
fn address_parts(a : @ir.Expr) -> (Int, @ir.Expr?, Int)? {
  match a.value() {
    Some(v) => return Some((v, None, 0x7ff))
    None => ()
  }
  match a {
    Unary(Byte, inner) =>
      match address_parts(inner) {
        Some((c, i, _)) => Some((c, i, 0xff))
        None => None
      }
    Binary(Add, l, r) =>
      match (l.value(), r.value()) {
        (Some(c), _) => Some((c, Some(r), 0x7ff))
        (_, Some(c)) => Some((c, Some(l), 0x7ff))
        _ => None
      }
    _ => None
  }
}

// The bytes an address can be, if it is a constant or a byte index added
// to one. The index of `(uint8_t)(base + x)` (zero page) or of `(base + x) &
// 0x7ff` does not wrap: --check-scratch checks it (page_guard,
// ram_end_guard), as records take it too.
fn CFunc::address_range(self : CFunc, a : @ir.Expr) -> (Int, Int)? {
  match a.value() {
    Some(v) => return Some((v & 0x7ff, v & 0x7ff))
    None => ()
  }
  let (inner, end) = match a {
    Unary(Byte, inner) => (inner, 0xff)
    Binary(And, inner, m) if m.value() == Some(0x7ff) => (inner, 0x7ff)
    other => (other, -1)
  }
  match address_parts(inner) {
    Some((c, Some(i), _)) if self.narrow(i) && c >= 0 =>
      if end > 0 { Some((c, if c + 255 < end { c + 255 } else { end })) }
      else if c + 255 <= 0x7ff { Some((c, c + 255)) }
      else { None }
    _ => None
  }
}

// Whether two RAM addresses are different bytes.
fn CFunc::distinct_ram(self : CFunc, a : @ir.Expr, b : @ir.Expr) -> Bool {
  match (address_parts(a), address_parts(b)) {
    (Some((c1, i1, m1)), Some((c2, i2, m2))) if i1 is Some(_) && i1 == i2 && m1 == m2 && ((c1 - c2) & m1) != 0 => return true
    _ => ()
  }
  match (self.address_range(a), self.address_range(b)) {
    (Some((lo1, hi1)), Some((lo2, hi2))) => hi1 < lo2 || hi2 < lo1
    _ => false
  }
}

// Whether the statements of block b strictly between `from` and `to` leave
// what the expressions read as it was, and define none of their temporaries.
fn CFunc::untouched(self : CFunc, b : Int, from : Int, to : Int, es : Array[@ir.Expr]) -> Bool {
  let vars = es.fold(init=0, fn(m, e) { m | e.vars() })
  let loads = []
  let temps = []
  for e in es { ram_loads(e, loads); e.temps(temps) }
  let other = es.iter().any(reads_other_memory)
  let stmts = self.f.blocks[b].stmts
  for k in (from + 1)..<to {
    match stmts[k] {
      Let(u, _) => if temps.contains(u) { return false }
      Assign(v, _) => if (vars & v.bit()) != 0 { return false }
      Store(Ram, a, _) => if other || !loads.iter().all(fn(l) { self.distinct_ram(a, l) }) { return false }
      _ => return false
    }
  }
  true
}

// The carry test `Temp(t) >= 0x100` (an addition) or `Temp(t) < 0` (a
// subtraction), and the sign it has in the high byte's sum.
fn carry_test(e : @ir.Expr) -> (Int, Bool)? {
  match e {
    Binary(Ge, Temp(t), c) if c.value() == Some(0x100) => Some((t, false))
    Binary(Lt, Temp(t), c) if c.value() == Some(0) => Some((t, true))
    _ => None
  }
}

// A load's index, to pair the bytes of one record: obj(x).page with obj(x).x.
fn load_index(e : @ir.Expr) -> @ir.Expr? {
  match e {
    Load(Ram, a) => match address_parts(a) { Some((_, i, _)) => i; None => None }
    _ => None
  }
}

// The carry chains of the function (see the top of the file).
fn CFunc::word_folds(self : CFunc) -> Map[Int, WordFold] {
  let folds : Map[Int, WordFold] = Map([])
  if !self.locals { return folds }
  // where each temporary is defined
  let defined : Map[Int, (Int, Int)] = Map([])
  for b, block in self.f.blocks {
    for k, s in block.stmts {
      if s is Let(t, _) { defined[t] = (b, k) }
    }
  }
  for b, block in self.f.blocks {
    for k, s in block.stmts {
      // the high byte: a register or a byte of memory
      let value = match s {
        Assign(v, e) if !v.is_flag() => e
        Store(_, a, e) if a.is_pure() => e
        _ => continue
      }
      let value = match value { Unary(Byte, e) => e; e => e }
      let high = []
      signed_terms(value, false, high)
      let tests = high.filter(fn(h) { carry_test(h.1) is Some(_) })
      guard tests.length() == 1 && carry_test(tests[0].1) is Some((t, borrow)) && tests[0].0 == borrow else { continue }
      guard defined.get(t) is Some((lb, lk)) && lb == b && lk < k && self.f.blocks[b].stmts[lk] is Let(_, sum) else { continue }
      guard !folds.contains(word_key(b, lk)) else { continue }
      let others = high.filter(fn(h) { carry_test(h.1) is None })
      guard !others.is_empty() && others.iter().all(fn(h) { h.1.uses_temp(t) == 0 && h.1.is_pure() }) else { continue }
      // the low sum's range: one carry, or one borrow
      let low = []
      signed_terms(sum, false, low)
      let mut lo = 0
      let mut hi = 0
      let mut bounded = true
      for term in low {
        match self.term_bounds(term.1) {
          Some((a, z)) => if term.0 { lo -= z; hi -= a } else { lo += a; hi += z }
          None => bounded = false
        }
      }
      guard bounded && (if borrow { lo >= -256 && hi <= 255 } else { lo >= 0 && hi <= 511 }) else { continue }
      guard only_low_uses(self.f, t, 1) else { continue }
      guard self.untouched(b, lk, k, others.map(fn(h) { h.1 })) else { continue }
      // each high term beside a low term of the same sign, of the same
      // record if there is one
      let paired : Array[@ir.Expr?] = low.map(fn(_) { None })
      let alone = []
      for h in others {
        let free = fn(i : Int) { paired[i] is None && low[i].0 == h.0 && !low[i].1.is_boolean() }
        let index = load_index(h.1)
        let mut found = -1
        for i in 0..<low.length() {
          if free(i) && index is Some(_) && load_index(low[i].1) == index { found = i; break }
        }
        if found < 0 {
          for i in 0..<low.length() {
            if free(i) { found = i; break }
          }
        }
        if found >= 0 { paired[found] = Some(h.1) } else { alone.push(h) }
      }
      folds[word_key(b, lk)] = Low(low.mapi(fn(i, l) { (l.0, l.1, paired[i]) }), alone, k)
      folds[word_key(b, k)] = High(t)
    }
    // 16-bit comparisons
    for k in 0..<block.stmts.length() {
      guard block.stmts[k] is Assign(C, Binary(Ge, d, z)) && z.value() == Some(0) && !folds.contains(word_key(b, k)) else { continue }
      let terms = []
      signed_terms(d, false, terms)
      guard terms.length() == 3 && !terms[0].0 && terms[1].0 && terms[2].0 else { continue }
      // the low bytes' borrow: compared just before, or in place
      let (l1, l2, k_low) = match terms[2].1 {
        Unary(Not, Var(C)) if k > 0 && !folds.contains(word_key(b, k - 1)) =>
          match block.stmts[k - 1] {
            Assign(C, Binary(Ge, l1, l2)) => (l1, l2, k - 1)
            _ => continue
          }
        Binary(Lt, l1, l2) => (l1, l2, k)
        _ => continue
      }
      let (h1, h2) = (terms[0].1, terms[1].1)
      let bytes = [l1, l2, h1, h2]
      guard bytes.iter().all(fn(e) { self.narrow(e) && !e.is_boolean() && e.is_pure() && e.uses_var(C) == 0 }) else { continue }
      if k_low < k { folds[word_key(b, k_low)] = Folded }
      folds[word_key(b, k)] = Compare(h1, l1, h2, l2, k_low)
    }
  }
  folds
}

// `WORD(hi, lo)`, or the constant; the high byte read where the chain took it.
fn CFunc::word_text(self : CFunc, b : Int, k_high : Int, h : @ir.Expr?, l : @ir.Expr?) -> String {
  let (block, stmt) = (self.block, self.stmt)
  let high = match h {
    Some(e) => { self.at(b, k_high); let text = self.expr(e); self.at(block, stmt); Some((e, text)) }
    None => None
  }
  let low = l.map(fn(e) { (e, self.expr(e)) })
  match (high, low) {
    (Some((he, ht)), Some((le, lt))) =>
      match (he.value(), le.value()) {
        (Some(hv), Some(lv)) => show_number(hv * 256 + lv)
        _ => "WORD(\{ht}, \{lt})"
      }
    (Some((he, ht)), None) => match he.value() { Some(hv) => show_number(hv * 256); None => "WORD(\{ht}, 0)" }
    (None, Some((le, _))) => self.paren(le, 4)
    (None, None) => "0"
  }
}

// The statement at (b, k) as a carry chain prints it, if it is part of one:
// Some("") when it is folded into another.
fn CFunc::word_stmt(self : CFunc, folds : Map[Int, WordFold], b : Int, k : Int, s : @ir.Stmt, live : Int) -> String? {
  match folds.get(word_key(b, k)) {
    None => None
    Some(Folded) => Some("")
    Some(High(t)) => {
      let byte : @ir.Expr = Unary(Byte, Binary(Shr, Temp(t), Const(8)))
      Some(match s {
        Assign(v, _) => self.stmt(Assign(v, byte), live~)
        Store(m, a, _) => self.stmt(Store(m, a, byte), live~)
        _ => self.stmt(s, live~)
      })
    }
    Some(Low(terms, alone, k_high)) => {
      guard s is Let(t, _) else { return None }
      let parts = []
      for term in terms { parts.push((term.0, self.word_text(b, k_high, term.2, Some(term.1)))) }
      for h in alone { parts.push((h.0, self.word_text(b, k_high, Some(h.1), None))) }
      let text = StringBuilder()
      for i, p in parts {
        if i == 0 { if p.0 { text.write_string("-") } } else { text.write_string(if p.0 { " - " } else { " + " }) }
        text.write_string(p.1)
      }
      Some("uint16_t \{self.names[t]} = (uint16_t)(\{text.to_string()});")
    }
    Some(Compare(h1, l1, h2, l2, k_low)) => {
      let (block, stmt) = (self.block, self.stmt)
      self.at(b, k_low)
      let (lt1, lt2) = (self.expr(l1), self.expr(l2))
      self.at(block, stmt)
      Some("\{self.reg(C)} = WORD(\{self.expr(h1)}, \{lt1}) >= WORD(\{self.expr(h2)}, \{lt2});")
    }
  }
}

