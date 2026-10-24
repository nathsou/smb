// A finite-height constant domain. Joins only lose facts, so cycles converge.
// Unknown is not zero; MMIO and unresolved memory reads always produce unknown.
//
// `depth` counts the bytes pushed since the routine's entry, when known: a
// return with two pushed is a computed jump.
//
// `banking` (MMC3 cartridges) follows what is known of the banks mapped, so
// that a call into a window goes to the bank known to be there.
pub(all) struct Facts {
  a: Int?
  x: Int?
  y: Int?
  c: Bool?
  z: Bool?
  n: Bool?
  v: Bool?
  depth: Int?
  banking: Banking?
} derive(Debug, Eq)

pub fn Facts::unknown() -> Facts {
  { a: None, x: None, y: None, c: None, z: None, n: None, v: None, depth: None, banking: None }
}

// What is known of the banks mapped: the registers' values, which unlike
// `a`, `x` and `y` may come from RAM (they never decide a branch: the
// interrupt can change RAM where the foreground waits), the RAM bytes known
// (internal RAM outside the stack page), the last bank select written and
// the bank in each window. Games keep the bank numbers in RAM and switch
// with short routines (lda #bank / sta shadow / jsr switch / jsr routine).
//
// A value is a set of the values it can have (`Values`): paths that meet
// keep both, and a load indexed into a table of the map can be any of its
// bytes (the banks of each level, say), up to a few; empty when unknown.
pub(all) struct Banking {
  a: Values
  x: Values
  y: Values
  ram: Array[(Int, Values)] // address and values, ascending, at most RAM_FACTS
  select: Int?
  banks: Array[Values] // by window
} derive(Debug, Eq)

// The values a byte can have, ascending; empty when unknown.
pub type Values = Array[Int]

let ram_facts : Int = 32
let most_values : Int = 32

fn single(v: Int) -> Values {
  [v]
}

pub fn values_of(list: Array[Int]) -> Values {
  let out = []
  for v in list {
    if !out.contains(v) { out.push(v) }
  }
  out.sort()
  if out.length() > most_values { [] } else { out }
}

fn join_values(a: Values, b: Values) -> Values {
  if a.is_empty() || b.is_empty() { return [] }
  let all = a.copy()
  all.append(b)
  values_of(all)
}

fn map_values(a: Values, f: (Int) -> Int) -> Values {
  values_of(a.map(f))
}

pub fn Banking::unknown() -> Banking {
  { a: [], x: [], y: [], ram: [], select: None, banks: [] }
}

pub fn Banking::join(self: Banking, other: Banking) -> Banking {
  let ram = []
  for entry in self.ram {
    match other.ram.iter().find_first(fn(o) { o.0 == entry.0 }) {
      Some(o) => {
        let v = join_values(entry.1, o.1)
        if !v.is_empty() { ram.push((entry.0, v)) }
      }
      None => ()
    }
  }
  let count = if self.banks.length() > other.banks.length() { self.banks.length() } else { other.banks.length() }
  let banks = Array::makei(count, fn(i) { join_values(self.bank(i), other.bank(i)) })
  { a: join_values(self.a, other.a), x: join_values(self.x, other.x), y: join_values(self.y, other.y),
    ram, select: if self.select == other.select { self.select } else { None }, banks }
}

// The banks window `window` can hold (empty: unknown).
pub fn Banking::bank(self: Banking, window: Int) -> Values {
  match self.banks.get(window) {
    Some(banks) => banks
    None => []
  }
}

fn Banking::read(self: Banking, address: Int) -> Values {
  match self.ram.iter().find_first(fn(entry) { entry.0 == address }) {
    Some(entry) => entry.1
    None => []
  }
}

// The internal RAM byte an address is, when tracked.
fn tracked(address: Int) -> Int? {
  if address < 0x2000 {
    let a = address & 0x7ff
    if a >= 0x100 && a < 0x200 { None } else { Some(a) }
  } else { None }
}

fn Banking::write(self: Banking, address: Int, value: Values) -> Banking {
  let ram = self.ram.filter(fn(entry) { entry.0 != address })
  if !value.is_empty() {
    ram.push((address, value))
    ram.sort_by(fn(l, r) { l.0 - r.0 })
    // the lowest addresses (zero-page temporaries, short-lived) go first
    while ram.length() > ram_facts { ram.remove(0) |> ignore }
  }
  { ..self, ram }
}

fn Banking::forget(self: Banking, lo: Int, hi: Int) -> Banking {
  { ..self, ram: self.ram.filter(fn(entry) { entry.0 < lo || entry.0 > hi }) }
}

// What survives a wait, or a call: the shadows (the RAM bytes with the
// banks the game wants, which the interrupt handlers keep) and, with
// `keep_banks`, the banks, but those of the windows `switched` (a bit each)
// names.
pub fn Banking::kept(self: Banking, shadows: Array[(Int, Int)], switched~: Int, keep_banks~: Bool = true) -> Banking {
  let ram = self.ram.filter(fn(entry) {
    shadows.iter().any(fn(s) { s.1 == entry.0 && (switched & (1 << s.0)) == 0 })
  })
  let banks = Array::makei(self.banks.length(), fn(i) { if keep_banks && (switched & (1 << i)) == 0 { self.bank(i) } else { [] } })
  { ..Banking::unknown(), ram, select: if keep_banks { self.select } else { None }, banks }
}

// The banks a window can hold: known, or else its shadow's.
pub fn Banking::mapped(self: Banking, window: Int, shadows: Array[(Int, Int)]) -> Values {
  let banks = self.bank(window)
  if !banks.is_empty() { return banks }
  match shadows.iter().find_first(fn(s) { s.0 == window }) {
    Some(s) => self.read(s.1)
    None => []
  }
}

// With the bank in window `window` known: code of that bank runs there.
pub fn Banking::running(self: Banking, window: Int, bank: Int) -> Banking {
  let banks = Array::makei(layout().windows.length(), fn(i) { if i == window { single(bank) } else { self.bank(i) } })
  { ..self, banks }
}

// After code that may switch any bank.
fn Banking::unmapped(self: Banking) -> Banking {
  { ..self, select: None, banks: [] }
}

// A write to MMC3's bank select ($8000, even) or bank data ($8001, odd).
fn Banking::mapper(self: Banking, address: Int, value: Values) -> Banking {
  if address >= 0xa000 { return self } // mirroring, RAM protection, IRQ
  if (address & 1) == 0 { return { ..self, select: if value.length() == 1 { Some(value[0]) } else { None } } }
  guard self.select is Some(select) else { return self.unmapped() }
  let register = select & 7
  if register < 6 { return self } // CHR
  // PRG mode 1: R7 at $A000, R6 at $C000; mode 0: R6 at $8000, R7 at $A000
  let start = if register == 7 { 0xa000 } else if (select & 0x40) != 0 { 0xc000 } else { 0x8000 }
  let window = window_of(start)
  if window < 0 { return self.unmapped() }
  let banks = Array::makei(layout().windows.length(), fn(i) { if i == window { value } else { self.bank(i) } })
  { ..self, banks }
}

// One instruction's effect on what is known (calls are the explorer's).
// `table` gives the bytes an indexed load from ROM can read: those of the
// map's table there, to its end.
pub fn Banking::transfer(self: Banking, inst: Instruction, table~: (Int) -> Values = fn(_) { [] }) -> Banking {
  let direct = inst.mode == "zp" || inst.mode == "abs"
  let loaded = if inst.mode == "imm" { single(inst.arg) }
    else if direct { match tracked(inst.arg) { Some(a) => self.read(a); None => [] } }
    else if (inst.mode == "absx" || inst.mode == "absy") && inst.arg >= 0x8000 { table(inst.pc) }
    else { [] }
  let step = fn(v: Values, d: Int) { map_values(v, fn(x) { (x + d) & 255 }) }
  // a store's place: tracked RAM, the mapper, or a range it may hit
  let store = fn(b: Banking, value: Values) -> Banking {
    match inst.mode {
      "zp" | "abs" =>
        match tracked(inst.arg) {
          Some(a) => b.write(a, value)
          None => if inst.arg >= 0x8000 { b.mapper(inst.arg, value) } else { b }
        }
      "zpx" | "zpy" => b.forget(0, 0xff)
      "absx" | "absy" => {
        let b = if inst.arg >= 0x8000 { b.unmapped() } else { b }
        if inst.arg < 0x2000 { b.forget(inst.arg & 0x7ff, (inst.arg & 0x7ff) + 255) } else { b }
      }
      _ => { ..b.unmapped(), ram: [] } // through a pointer: anywhere
    }
  }
  match inst.op {
    LDA => { ..self, a: loaded }
    LDX => { ..self, x: loaded }
    LDY => { ..self, y: loaded }
    TAX => { ..self, x: self.a }
    TAY => { ..self, y: self.a }
    TXA => { ..self, a: self.x }
    TYA => { ..self, a: self.y }
    INX => { ..self, x: step(self.x, 1) }
    DEX => { ..self, x: step(self.x, -1) }
    INY => { ..self, y: step(self.y, 1) }
    DEY => { ..self, y: step(self.y, -1) }
    AND | ORA | EOR => {
      let a = if loaded.length() == 1 {
        let b = loaded[0]
        map_values(self.a, fn(a) { match inst.op { AND => a & b; ORA => a | b; _ => a ^ b } })
      } else { [] }
      { ..self, a }
    }
    ASL | LSR | ROL | ROR | ADC | SBC | PLA =>
      if inst.mode == "acc" || inst.op == ADC || inst.op == SBC || inst.op == PLA { { ..self, a: [] } }
      else { store(self, []) }
    TSX => { ..self, x: [] }
    STA => store(self, self.a)
    STX => store(self, self.x)
    STY => store(self, self.y)
    INC | DEC =>
      match (inst.mode, tracked(inst.arg)) {
        ("zp" | "abs", Some(a)) => self.write(a, step(self.read(a), if inst.op == INC { 1 } else { -1 }))
        _ => store(self, [])
      }
    _ => self
  }
}

// At the entry of a routine.
pub fn Facts::entry(self: Facts) -> Facts {
  { ..self, depth: Some(0) }
}

pub fn Facts::join(self: Facts, other: Facts) -> Facts {
  { a: if self.a == other.a { self.a } else { None },
    x: if self.x == other.x { self.x } else { None },
    y: if self.y == other.y { self.y } else { None },
    c: if self.c == other.c { self.c } else { None },
    z: if self.z == other.z { self.z } else { None },
    n: if self.n == other.n { self.n } else { None },
    v: if self.v == other.v { self.v } else { None },
    depth: if self.depth == other.depth { self.depth } else { None },
    banking: match (self.banking, other.banking) {
      (Some(a), Some(b)) => Some(a.join(b))
      _ => None
    } }
}

fn Facts::nz(self: Facts, value: Int?) -> Facts {
  { ..self, z: value.map(fn(v) { v == 0 }), n: value.map(fn(v) { (v & 128) != 0 }) }
}

pub fn condition(op: @parse.OpCode, f: Facts) -> Bool? {
  match op {
    BEQ => f.z
    BNE => f.z.map(fn(v) { !v })
    BCS => f.c
    BCC => f.c.map(fn(v) { !v })
    BMI => f.n
    BPL => f.n.map(fn(v) { !v })
    BVS => f.v
    BVC => f.v.map(fn(v) { !v })
    _ => None
  }
}

pub fn assume_condition(op: @parse.OpCode, taken: Bool, f: Facts) -> Facts {
  match op {
    BEQ => { ..f, z: Some(taken) }
    BNE => { ..f, z: Some(!taken) }
    BCS => { ..f, c: Some(taken) }
    BCC => { ..f, c: Some(!taken) }
    BMI => { ..f, n: Some(taken) }
    BPL => { ..f, n: Some(!taken) }
    BVS => { ..f, v: Some(taken) }
    BVC => { ..f, v: Some(!taken) }
    _ => f
  }
}

pub fn transfer(inst: Instruction, f: Facts, table~: (Int) -> Values = fn(_) { [] }) -> Facts {
  let after = transfer_registers(inst, f)
  match f.banking {
    Some(b) if inst.op != JSR => { ..after, banking: Some(b.transfer(inst, table~)) }
    _ => after
  }
}

fn transfer_registers(inst: Instruction, f: Facts) -> Facts {
  let operand = if inst.mode == "imm" { Some(inst.arg) } else { None }
  match inst.op {
    LDA => { ..f.nz(operand), a: operand }
    LDX => { ..f.nz(operand), x: operand }
    LDY => { ..f.nz(operand), y: operand }
    TAX => { ..f.nz(f.a), x: f.a }
    TAY => { ..f.nz(f.a), y: f.a }
    TXA => { ..f.nz(f.x), a: f.x }
    TYA => { ..f.nz(f.y), a: f.y }
    INX | DEX => {
      let x = f.x.map(fn(v) { (v + (if inst.op == INX { 1 } else { -1 })) & 255 })
      { ..f.nz(x), x }
    }
    INY | DEY => {
      let y = f.y.map(fn(v) { (v + (if inst.op == INY { 1 } else { -1 })) & 255 })
      { ..f.nz(y), y }
    }
    CLC => { ..f, c: Some(false) }
    SEC => { ..f, c: Some(true) }
    CLV => { ..f, v: Some(false) }
    AND | ORA | EOR => {
      let a = match (f.a, operand) {
        (Some(a), Some(b)) => Some(match inst.op { AND => a & b; ORA => a | b; _ => a ^ b })
        _ => if inst.op == AND && operand == Some(0) { Some(0) } else { None }
      }
      { ..f.nz(a), a }
    }
    CMP | CPX | CPY => {
      let reg = match inst.op { CMP => f.a; CPX => f.x; _ => f.y }
      match (reg, operand) {
        (Some(a), Some(b)) => { ..f.nz(Some((a - b) & 255)), c: Some(a >= b) }
        _ => { ..f.nz(None), c: None }
      }
    }
    ASL | LSR | ROL | ROR => {
      let input = if inst.mode == "acc" { f.a } else { None }
      let carry = input.map(fn(v) { (v & (if inst.op == ASL || inst.op == ROL { 128 } else { 1 })) != 0 })
      let a = match (input, inst.op, f.c) {
        (Some(v), ASL, _) => Some((v << 1) & 255)
        (Some(v), LSR, _) => Some(v >> 1)
        (Some(v), ROL, Some(c)) => Some(((v << 1) | (if c { 1 } else { 0 })) & 255)
        (Some(v), ROR, Some(c)) => Some((v >> 1) | (if c { 128 } else { 0 }))
        _ => None
      }
      { ..f.nz(a), c: carry, a: if inst.mode == "acc" { a } else { f.a } }
    }
    ADC | SBC => { ..f.nz(None), a: None, c: None, v: None }
    BIT => { ..f.nz(None), v: None }
    PLA => { ..f.nz(None), a: None, depth: f.depth.map(fn(d) { d - 1 }) }
    PHA | PHP => { ..f, depth: f.depth.map(fn(d) { d + 1 }) }
    TSX => { ..f.nz(None), x: None }
    TXS => { ..f, depth: None }
    INC | DEC => f.nz(None)
    PLP => { ..f, c: None, z: None, n: None, v: None, depth: f.depth.map(fn(d) { d - 1 }) }
    RTI => { ..f, c: None, z: None, n: None, v: None }
    // a call returns with the stack as it found it, and may have switched
    // banks (the explorer evaluates short callees instead)
    JSR => { ..Facts::unknown(), depth: f.depth, banking: f.banking.map(fn(_) { Banking::unknown() }) }
    _ => f
  }
}
