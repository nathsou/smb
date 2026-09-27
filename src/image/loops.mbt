// The frame-based runtime delivers NMI only at a `jmp *` idle yield. A loop
// that can come back to the same instruction without changing memory, reading
// I/O, touching the stack or consuming a register it later changes would
// repeat that iteration forever: only an interrupt could end it. Such
// wait-for-interrupt loops need a resumable foreground, so they are rejected
// instead of compiling into a hang.

// Register and flag bits: A, X, Y, C, Z, N, V.
const REG_A = 1
const REG_X = 2
const REG_Y = 4
const FLAG_C = 8
const FLAG_Z = 16
const FLAG_N = 32
const FLAG_V = 64
const ALL_STATE = 127

fn plain_read(inst: Instruction) -> Bool {
  match inst.mode {
    "imp" | "acc" | "imm" | "rel" | "zp" | "zpx" | "zpy" => true
    "abs" => inst.arg < 0x2000 || inst.arg >= 0x8000
    "absx" | "absy" => inst.arg + 255 < 0x2000 || inst.arg >= 0x8000
    _ => false // pointers may address I/O
  }
}

// (reads, writes) for instructions without external effects, else None.
fn pure_effects(inst: Instruction) -> (Int, Int)? {
  if !plain_read(inst) { return None }
  let index = match inst.mode {
    "zpx" | "absx" => REG_X
    "zpy" | "absy" => REG_Y
    _ => 0
  }
  let nz = FLAG_Z | FLAG_N
  let effects = match inst.op {
    LDA => (index, REG_A | nz)
    LDX => (index, REG_X | nz)
    LDY => (index, REG_Y | nz)
    AND | ORA | EOR => (REG_A | index, REG_A | nz)
    ADC | SBC => (REG_A | FLAG_C | index, REG_A | FLAG_C | FLAG_V | nz)
    CMP => (REG_A | index, FLAG_C | nz)
    CPX => (REG_X, FLAG_C | nz)
    CPY => (REG_Y, FLAG_C | nz)
    BIT => (REG_A, FLAG_V | nz)
    ASL | LSR if inst.mode == "acc" => (REG_A, REG_A | FLAG_C | nz)
    ROL | ROR if inst.mode == "acc" => (REG_A | FLAG_C, REG_A | FLAG_C | nz)
    TAX => (REG_A, REG_X | nz)
    TAY => (REG_A, REG_Y | nz)
    TXA => (REG_X, REG_A | nz)
    TYA => (REG_Y, REG_A | nz)
    TSX => (0, REG_X | nz)
    INX | DEX => (REG_X, REG_X | nz)
    INY | DEY => (REG_Y, REG_Y | nz)
    CLC | SEC => (0, FLAG_C)
    CLV => (0, FLAG_V)
    NOP | CLD | SED | SEI => (0, 0)
    BCC | BCS => (FLAG_C, 0)
    BEQ | BNE => (FLAG_Z, 0)
    BMI | BPL => (FLAG_N, 0)
    BVC | BVS => (FLAG_V, 0)
    JMP if inst.mode == "abs" => (0, 0)
    _ => return None
  }
  Some(effects)
}

// From each pure instruction p, follow pure paths while tracking which state
// bits still hold p's incoming values and which of those were consumed. If a
// path returns to p having changed nothing it consumed, every later iteration
// consumes the same values and repeats the same path.
fn check_wait_loops(image: Image, instructions: Map[Int, Instruction], facts: Map[Int, Facts], idle: Set[Int]) -> Unit raise ImageError {
  fn successors(inst: Instruction) -> Array[Int] {
    if inst.op == JMP { return [inst.arg] }
    if inst.mode != "rel" { return [inst.next] }
    match condition(inst.op, facts[inst.pc]) {
      Some(true) => [branch_target(inst)]
      Some(false) => [inst.next]
      None => [branch_target(inst), inst.next]
    }
  }
  fn pure(pc: Int) -> (Int, Int)? {
    if idle.contains(pc) { return None }
    match instructions.get(pc) { Some(inst) => pure_effects(inst); None => None }
  }
  for start, _ in instructions {
    if pure(start) is None { continue }
    let seen: Set[Int] = Set([])
    let work = [(start, ALL_STATE, 0)]
    while work.pop() is Some((pc, untouched, consumed)) {
      let key = pc * 16384 + untouched * 128 + consumed
      if seen.contains(key) { continue }
      seen.add(key)
      let (reads, writes) = pure(pc).unwrap()
      let consumed = consumed | (reads & untouched)
      let untouched = untouched & (ALL_STATE ^ writes)
      for next in successors(instructions[pc]) {
        if next == start {
          if (consumed & (ALL_STATE ^ untouched)) == 0 {
            raise InvalidImage("Loop at \{show_pc(start)} (\{address_name(image, start)}) can only exit through an interrupt; the frame-based backend requires a `jmp *` idle loop")
          }
        } else if pure(next) is Some(_) {
          work.push((next, untouched, consumed))
        }
      }
    }
  }
}
