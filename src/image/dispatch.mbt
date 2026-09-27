// Inline dispatch: `jsr Helper` followed by table data that the helper reads
// through its own return address. Nothing here depends on the helper's
// instruction sequence. The helper is evaluated as ordinary 6502 code over a
// partially known machine: only the index register, the call's return token
// and immutable PRG bytes are known; other registers, flags and RAM start
// unknown. Any decision or address that depends on an unknown fails the proof.

pub(all) struct DispatchStep {
  inst: Instruction
  taken: Bool // meaningful for conditional branches only
} derive(Debug)

pub(all) enum DispatchExit {
  IndirectJump(Int) // JMP (pointer)
  ReturnJump // RTS through two bytes pushed by the helper
} derive(Debug, Eq)

pub(all) struct Dispatch {
  call_pc: Int
  helper: Int
  steps: Array[DispatchStep]
  exit: DispatchExit
  targets: Array[Int]
  table_end: Int
} derive(Debug)

pub fn show_pc(pc: Int) -> String {
  "$" + pc.to_string(radix=16).pad_start(4, '0')
}

priv struct Machine {
  image: Image
  regs: Array[Int?] // A, X, Y
  mut c: Bool?
  mut z: Bool?
  mut n: Bool?
  mut v: Bool?
  ram: Map[Int, Int?]
  stack: Map[Int, Int?] // relative to SP after the JSR; token at +1/+2
  mut sp: Int
  mut max_sp: Int
  reads: Array[Int]
}

priv suberror Unproven

fn Machine::load(self: Machine, address: Int) -> Int? raise Unproven {
  let address = address & 0xffff
  if address < 0x2000 {
    let ram = address & 0x7ff
    // SP is unknown in absolute terms, so the stack page cannot be modelled.
    if ram >= 0x100 && ram < 0x200 { return None }
    self.ram.get(ram).unwrap_or(None)
  } else if address >= 0x8000 {
    let value = try self.image.read(address) catch { _ => raise Unproven }
    self.reads.push(address)
    Some(value)
  } else { None } // MMIO and open bus
}

fn Machine::store(self: Machine, address: Int, value: Int?) -> Unit raise Unproven {
  let address = address & 0xffff
  if address < 0x2000 {
    let ram = address & 0x7ff
    if ram >= 0x100 && ram < 0x200 { raise Unproven }
    self.ram[ram] = value
  }
  // Writes to MMIO are performed by the emitted helper; they do not change
  // any value this proof reads.
}

fn[T] known(value: T?) -> T raise Unproven {
  match value { Some(v) => v; None => raise Unproven }
}

fn Machine::address(self: Machine, inst: Instruction) -> Int raise Unproven {
  match inst.mode {
    "zp" | "abs" => inst.arg
    "zpx" => (inst.arg + known(self.regs[1])) & 255
    "zpy" => (inst.arg + known(self.regs[2])) & 255
    "absx" => (inst.arg + known(self.regs[1])) & 0xffff
    "absy" => (inst.arg + known(self.regs[2])) & 0xffff
    "indx" => {
      let pointer = (inst.arg + known(self.regs[1])) & 255
      known(self.load(pointer)) | (known(self.load((pointer + 1) & 255)) << 8)
    }
    "indy" => {
      let base = known(self.load(inst.arg)) | (known(self.load((inst.arg + 1) & 255)) << 8)
      (base + known(self.regs[2])) & 0xffff
    }
    _ => raise Unproven
  }
}

fn Machine::operand(self: Machine, inst: Instruction) -> Int? raise Unproven {
  if inst.mode == "imm" { Some(inst.arg) } else { self.load(self.address(inst)) }
}

fn Machine::nz(self: Machine, value: Int?) -> Unit {
  self.z = value.map(fn(v) { v == 0 })
  self.n = value.map(fn(v) { (v & 128) != 0 })
}

fn Machine::push(self: Machine, value: Int?) -> Unit {
  self.stack[self.sp] = value
  self.sp -= 1
}

fn Machine::pull(self: Machine) -> Int? raise Unproven {
  self.sp += 1
  // Reading above the call token would inspect the caller's frame.
  if self.sp > 2 { raise Unproven }
  self.max_sp = if self.sp > self.max_sp { self.sp } else { self.max_sp }
  self.stack.get(self.sp).unwrap_or(None)
}

fn[T] both(a: T?, b: T?) -> (T, T)? {
  match (a, b) { (Some(x), Some(y)) => Some((x, y)); _ => None }
}

priv struct Run {
  target: Int
  steps: Array[DispatchStep]
  exit: DispatchExit
  reads: Array[Int]
}

// Execute one index. Returns None when any needed value is unknown or the
// helper leaves through anything other than a transfer that consumed the
// call's own return token.
fn evaluate(image: Image, helper: Int, return_pc: Int, carrier: Int, index: Int) -> Run? {
  let m: Machine = {
    image, regs: [None, None, None], c: None, z: None, n: None, v: None,
    ram: Map([]), stack: Map([]), sp: 0, max_sp: 0, reads: [],
  }
  m.regs[carrier] = Some(index)
  m.stack[1] = Some(return_pc & 255)
  m.stack[2] = Some(return_pc >> 8)
  let steps = []
  let mut pc = helper
  try {
    for _ in 0..<256 {
      let inst = decode(image, pc) catch { _ => raise Unproven }
      let mut next = inst.next
      let mut taken = false
      match inst.op {
        LDA | LDX | LDY => {
          let value = m.operand(inst)
          m.regs[match inst.op { LDA => 0; LDX => 1; _ => 2 }] = value
          m.nz(value)
        }
        STA => m.store(m.address(inst), m.regs[0])
        STX => m.store(m.address(inst), m.regs[1])
        STY => m.store(m.address(inst), m.regs[2])
        TAX | TAY | TXA | TYA => {
          let (from, to) = match inst.op { TAX => (0, 1); TAY => (0, 2); TXA => (1, 0); _ => (2, 0) }
          m.regs[to] = m.regs[from]
          m.nz(m.regs[to])
        }
        TSX => { m.regs[1] = None; m.nz(None) }
        INX | DEX | INY | DEY => {
          let r = if inst.op == INX || inst.op == DEX { 1 } else { 2 }
          let delta = if inst.op == INX || inst.op == INY { 1 } else { 255 }
          m.regs[r] = m.regs[r].map(fn(v) { (v + delta) & 255 })
          m.nz(m.regs[r])
        }
        INC | DEC => {
          let address = m.address(inst)
          let delta = if inst.op == INC { 1 } else { 255 }
          let value = m.load(address).map(fn(v) { (v + delta) & 255 })
          m.store(address, value)
          m.nz(value)
        }
        ASL | LSR | ROL | ROR => {
          let address = if inst.mode == "acc" { None } else { Some(m.address(inst)) }
          let input = match address { None => m.regs[0]; Some(a) => m.load(a) }
          let left = inst.op == ASL || inst.op == ROL
          let carry_in = match inst.op { ROL | ROR => m.c.map(fn(c) { if c { 1 } else { 0 } }); _ => Some(0) }
          let result = both(input, carry_in).map(fn(p) {
            let (v, c) = p
            if left { ((v << 1) | c) & 255 } else { (v >> 1) | (c << 7) }
          })
          m.c = input.map(fn(v) { (v & (if left { 128 } else { 1 })) != 0 })
          match address { None => m.regs[0] = result; Some(a) => m.store(a, result) }
          m.nz(result)
        }
        AND | ORA | EOR => {
          let value = both(m.regs[0], m.operand(inst)).map(fn(p) {
            let (a, b) = p
            match inst.op { AND => a & b; ORA => a | b; _ => a ^ b }
          })
          m.regs[0] = value
          m.nz(value)
        }
        ADC | SBC => {
          let operand = m.operand(inst).map(fn(v) { if inst.op == SBC { v ^ 255 } else { v } })
          match (m.regs[0], operand, m.c) {
            (Some(a), Some(b), Some(c)) => {
              let sum = a + b + (if c { 1 } else { 0 })
              m.c = Some(sum > 255)
              m.v = Some(((a ^ sum) & (b ^ sum) & 128) != 0)
              m.regs[0] = Some(sum & 255)
            }
            _ => { m.regs[0] = None; m.c = None; m.v = None }
          }
          m.nz(m.regs[0])
        }
        CMP | CPX | CPY => {
          let r = match inst.op { CMP => 0; CPX => 1; _ => 2 }
          let pair = both(m.regs[r], m.operand(inst))
          m.c = pair.map(fn(p) { p.0 >= p.1 })
          m.nz(pair.map(fn(p) { (p.0 - p.1) & 255 }))
        }
        BIT => {
          let value = m.operand(inst)
          m.z = both(m.regs[0], value).map(fn(p) { (p.0 & p.1) == 0 })
          m.n = value.map(fn(v) { (v & 128) != 0 })
          m.v = value.map(fn(v) { (v & 64) != 0 })
        }
        CLC => m.c = Some(false)
        SEC => m.c = Some(true)
        CLV => m.v = Some(false)
        CLD | SED | SEI | CLI | NOP => ()
        PHA => m.push(m.regs[0])
        PHP => m.push(None) // I/D/B are not modelled
        PLA => { m.regs[0] = m.pull(); m.nz(m.regs[0]) }
        PLP => {
          let p = m.pull()
          m.c = p.map(fn(v) { (v & 1) != 0 })
          m.z = p.map(fn(v) { (v & 2) != 0 })
          m.v = p.map(fn(v) { (v & 64) != 0 })
          m.n = p.map(fn(v) { (v & 128) != 0 })
        }
        BCC | BCS | BEQ | BNE | BMI | BPL | BVC | BVS => {
          let flag = match inst.op { BCC | BCS => m.c; BEQ | BNE => m.z; BMI | BPL => m.n; _ => m.v }
          let set = known(flag)
          taken = match inst.op { BCS | BEQ | BMI | BVS => set; _ => !set }
          if taken { next = branch_target(inst) }
        }
        JMP if inst.mode == "abs" => next = inst.arg
        JMP => {
          let high = (inst.arg & 0xff00) | ((inst.arg + 1) & 255)
          let target = known(m.load(inst.arg)) | (known(m.load(high)) << 8)
          if m.sp != 2 { raise Unproven }
          return Some({ target, steps, exit: IndirectJump(inst.arg), reads: m.reads })
        }
        RTS => {
          // The helper must have consumed the call token before pushing the
          // destination; returning through the untouched token is a call.
          if m.max_sp < 2 { raise Unproven }
          let lo = known(m.pull())
          let hi = known(m.pull())
          if m.sp != 2 { raise Unproven }
          return Some({ target: ((lo | (hi << 8)) + 1) & 0xffff, steps, exit: ReturnJump, reads: m.reads })
        }
        JSR | RTI | BRK | TXS => raise Unproven
      }
      steps.push({ inst, taken })
      pc = next
    }
    None
  } catch { _ => None }
}

fn same_path(a: Array[DispatchStep], b: Array[DispatchStep]) -> Bool {
  a.length() == b.length() &&
  a.iter().zip(b.iter()).all(fn(p) { p.0.inst.pc == p.1.inst.pc && p.0.taken == p.1.taken })
}

// The table extent is the inline data after the call, up to the next label or
// instruction in the source. An index is accepted only when its evaluation
// reads PRG data inside that extent and follows the same path as index 0.
// Other indices are left to the emitted runtime trap.
fn dispatch_at(image: Image, call: Instruction, table_end: Int) -> Dispatch? {
  let within = fn(address: Int) { address >= call.next && address < table_end }
  for carrier in 0..<3 {
    let first = match evaluate(image, call.arg, call.next - 1, carrier, 0) {
      Some(run) if run.reads.iter().all(within) && !within(run.target) => run
      _ => continue
    }
    let targets = []
    for index in 0..<256 {
      match evaluate(image, call.arg, call.next - 1, carrier, index) {
        Some(run) if run.exit == first.exit && same_path(run.steps, first.steps) &&
          run.reads.iter().all(within) && !within(run.target) => {
          if !targets.contains(run.target) { targets.push(run.target) }
        }
        _ => ()
      }
    }
    return Some({ call_pc: call.pc, helper: call.arg, steps: first.steps, exit: first.exit, targets, table_end })
  }
  None
}

// End of the data directives starting at source item `start`: the table
// stops at the next label, instruction or origin change.
fn inline_data_end(image: Image, start: Int) -> Int {
  let mut end = image.addresses[start]
  for i in start..<image.source.length() {
    match image.source[i] {
      Directive(name="db" | "dw" as name, args~) if image.addresses[i] == end =>
        end += args.length() * (if name == "dw" { 2 } else { 1 })
      Comment(_) | Define(_) => ()
      _ => break
    }
  }
  end
}
