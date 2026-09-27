// A finite-height constant domain. Joins only lose facts, so cycles converge.
// Unknown is not zero; MMIO and unresolved memory reads always produce unknown.
pub(all) struct Facts {
  a: Int?
  x: Int?
  y: Int?
  c: Bool?
  z: Bool?
  n: Bool?
  v: Bool?
} derive(Debug, Eq)

pub fn Facts::unknown() -> Facts {
  { a: None, x: None, y: None, c: None, z: None, n: None, v: None }
}

pub fn Facts::join(self: Facts, other: Facts) -> Facts {
  { a: if self.a == other.a { self.a } else { None },
    x: if self.x == other.x { self.x } else { None },
    y: if self.y == other.y { self.y } else { None },
    c: if self.c == other.c { self.c } else { None },
    z: if self.z == other.z { self.z } else { None },
    n: if self.n == other.n { self.n } else { None },
    v: if self.v == other.v { self.v } else { None } }
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

pub fn transfer(inst: Instruction, f: Facts) -> Facts {
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
    PLA => { ..f.nz(None), a: None }
    TSX => { ..f.nz(None), x: None }
    INC | DEC => f.nz(None)
    PLP | RTI => { ..f, c: None, z: None, n: None, v: None }
    JSR => Facts::unknown()
    _ => f
  }
}
