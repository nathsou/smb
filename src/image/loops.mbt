// The frame-based runtime delivers NMI only where the foreground yields: at a
// `jmp *` idle loop, or in a wait loop. A loop that can come back to the
// same instruction without changing memory, reading I/O, touching the stack
// or consuming a register it later changes would repeat that iteration
// forever: only an interrupt could end it.
//
// A path back whose decisions depend on what it read from memory is not
// proven to repeat forever: what it reads decides (a table that restarts an
// animation, say). Among those, a wait loop is a loop entered only at its
// start, whose every decision
// depends on values it read from memory since the start: memory does not
// change during the loop, so once an iteration from the start comes back to
// it, every later one would too. The transfer back to the start yields
// instead: the runtime delivers the interrupt, and the loop resumes from its
// start, reading memory again (the foreground keeps its state; see the IR's
// resumable functions). Other loops that only an interrupt could end are
// rejected instead of compiling into a hang.

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
// consumes the same values and repeats the same path. Paths follow what they
// know of the registers and flags (from p on), so that a path no execution
// takes is not one (`tya; bne out` leaves A zero, which a comparison then
// knows).
fn check_wait_loops(image: Image, instructions: Map[Int, Instruction], facts: Map[Int, Facts], idle: Set[Int], waits: Map[Int, Int]) -> Unit raise ImageError {
  let waiting: Set[Int] = Set([])
  for _, start in waits {
    for pc in wait_body(instructions, facts, idle, start) { waiting.add(pc) }
  }
  fn pure(pc: Int) -> (Int, Int)? {
    if idle.contains(pc) { return None }
    match instructions.get(pc) { Some(inst) => pure_effects(inst); None => None }
  }
  for start, _ in instructions {
    if pure(start) is None || waiting.contains(start) { continue }
    // `tainted`: state holding values read from memory on this path;
    // `decided`: whether a branch on this path depended on one
    let seen: Set[(Int, Int, Int, Int, Bool, PathFacts)] = Set([])
    let work = [(start, ALL_STATE, 0, 0, false, PathFacts::unknown())]
    while work.pop() is Some((pc, untouched, consumed, tainted, decided, known)) {
      if seen.contains((pc, untouched, consumed, tainted, decided, known)) { continue }
      // (paths that keep counting are not followed for ever: as before)
      if seen.length() > 20000 { break }
      seen.add((pc, untouched, consumed, tainted, decided, known))
      let inst = instructions[pc]
      let (reads, writes) = pure(pc).unwrap()
      let consumed = consumed | (reads & untouched)
      let untouched = untouched & (ALL_STATE ^ writes)
      let decided = decided || (inst.mode == "rel" && (reads & tainted) != 0)
      let tainted = if reads_memory(inst) || (reads & tainted) != 0 { tainted | writes } else { tainted & (ALL_STATE ^ writes) }
      for step in known.successors(inst, facts) {
        let (next, after) = step
        if next == start {
          if (consumed & (ALL_STATE ^ untouched)) == 0 && !decided {
            raise InvalidImage("Loop at \{show_pc(start)} (\{address_name(image, start)}) can only exit through an interrupt; the frame-based backend requires a `jmp *` idle loop")
          }
        } else if pure(next) is Some(_) {
          work.push((next, untouched, consumed, tainted, decided, after))
        }
      }
    }
  }
}

// What a path knows of the registers and flags, and which register the Z
// flag tells of (REG_A, REG_X, REG_Y, or 0): a branch on it tells whether
// that register is zero.
priv struct PathFacts {
  a: Int?
  x: Int?
  y: Int?
  c: Bool?
  z: Bool?
  n: Bool?
  v: Bool?
  zero_of: Int
} derive(Eq, Hash)

fn PathFacts::unknown() -> PathFacts {
  { a: None, x: None, y: None, c: None, z: None, n: None, v: None, zero_of: 0 }
}

fn PathFacts::facts(self: PathFacts) -> Facts {
  { ..Facts::unknown(), a: self.a, x: self.x, y: self.y, c: self.c, z: self.z, n: self.n, v: self.v }
}

fn path_facts(f: Facts, zero_of: Int) -> PathFacts {
  { a: f.a, x: f.x, y: f.y, c: f.c, z: f.z, n: f.n, v: f.v, zero_of }
}

// The successors a path can take, with what it knows after the instruction.
fn PathFacts::successors(self: PathFacts, inst: Instruction, facts: Map[Int, Facts]) -> Array[(Int, PathFacts)] {
  let after = transfer(inst, self.facts())
  let zero_of = match inst.op {
    LDA | TXA | TYA | AND | ORA | EOR | PLA => REG_A
    LDX | TAX | INX | DEX | TSX => REG_X
    LDY | TAY | INY | DEY => REG_Y
    _ => if (match pure_effects(inst) { Some((_, writes)) => (writes & FLAG_Z) != 0; None => true }) { 0 } else { self.zero_of }
  }
  let known = path_facts(after, zero_of)
  if inst.op == JMP { return [(jump_target(inst), known)] }
  if inst.mode != "rel" { return [(inst.next, known)] }
  let outcome = match condition(inst.op, facts[inst.pc]) {
    Some(decided) => Some(decided)
    None => condition(inst.op, self.facts())
  }
  let taken = fn(yes: Bool) {
    let f = assume_condition(inst.op, yes, self.facts())
    // a register the Z flag says is zero
    let f = if (inst.op == BEQ || inst.op == BNE) && f.z == Some(true) {
      match zero_of {
        1 => { ..f, a: Some(0), n: Some(false) }
        2 => { ..f, x: Some(0), n: Some(false) }
        4 => { ..f, y: Some(0), n: Some(false) }
        _ => f
      }
    } else { f }
    path_facts(f, zero_of)
  }
  match outcome {
    Some(true) => [(branch_target(inst), taken(true))]
    Some(false) => [(inst.next, taken(false))]
    None => [(branch_target(inst), taken(true)), (inst.next, taken(false))]
  }
}

fn reads_memory(inst: Instruction) -> Bool {
  match inst.mode {
    "zp" | "zpx" | "zpy" | "abs" | "absx" | "absy" | "indx" | "indy" => inst.op != JMP
    _ => false
  }
}

fn successors(inst: Instruction, facts: Map[Int, Facts]) -> Array[Int] {
  if inst.op == JMP { return [jump_target(inst)] }
  if inst.mode != "rel" { return [inst.next] }
  match condition(inst.op, facts[inst.pc]) {
    Some(true) => [branch_target(inst)]
    Some(false) => [inst.next]
    None => [branch_target(inst), inst.next]
  }
}

fn pure_at(instructions: Map[Int, Instruction], idle: Set[Int], pc: Int) -> (Int, Int)? {
  if idle.contains(pc) { return None }
  match instructions.get(pc) {
    Some(inst) if inst.op != JMP || inst.mode == "abs" => pure_effects(inst)
    _ => None
  }
}

// The pure instructions on paths from `start` back to it.
fn wait_body(instructions: Map[Int, Instruction], facts: Map[Int, Facts], idle: Set[Int], start: Int) -> Array[Int] {
  let forward: Set[Int] = Set([])
  let work = [start]
  while work.pop() is Some(pc) {
    if forward.contains(pc) || pure_at(instructions, idle, pc) is None { continue }
    forward.add(pc)
    for next in successors(instructions[pc], facts) { work.push(next) }
  }
  // keep those from which the start is reachable again
  let body: Set[Int] = Set([])
  let mut changed = true
  while changed {
    changed = false
    for pc in forward {
      if body.contains(pc) { continue }
      if successors(instructions[pc], facts).iter().any(fn(n) { n == start || body.contains(n) }) {
        body.add(pc)
        changed = true
      }
    }
  }
  let out = body.to_array()
  out.sort()
  out
}

// Wait loops: each transfer back to a wait loop's start, with that start.
fn find_waits(instructions: Map[Int, Instruction], facts: Map[Int, Facts], idle: Set[Int]) -> Map[Int, Int] {
  // Where control can come from: other instructions, and entries (calls,
  // vectors, dispatch and jump targets) that make an instruction a start.
  let predecessors: Map[Int, Array[Int]] = Map([])
  let entered: Set[Int] = Set([])
  for pc, inst in instructions {
    let next = match (inst.op, inst.mode) {
      (JSR, _) => {
        entered.add(jump_target(inst))
        [inst.next]
      }
      (JMP, "ind") | (RTS, _) | (RTI, _) | (BRK, _) => []
      _ => successors(inst, facts)
    }
    for n in next {
      if !predecessors.contains(n) { predecessors[n] = [] }
      predecessors[n].push(pc)
    }
  }
  let waits: Map[Int, Int] = Map([])
  let claimed: Set[Int] = Set([])
  let starts = instructions.keys().to_array()
  starts.sort()
  for start in starts {
    if claimed.contains(start) || pure_at(instructions, idle, start) is None { continue }
    let body = wait_body(instructions, facts, idle, start)
    if !body.contains(start) { continue }
    let inside: Set[Int] = Set(body)
    // entered only at the start
    let closed = body.iter().all(fn(pc) {
      pc == start || (!entered.contains(pc) && predecessors.get(pc).unwrap_or([]).iter().all(fn(p) { inside.contains(p) }))
    })
    if !closed { continue }
    // Registers and flags holding values read from memory (or constants)
    // since the start, on every path within the body.
    let determined: Map[Int, Int] = Map([])
    determined[start] = 0
    let mut changed = true
    while changed {
      changed = false
      for pc in body {
        guard determined.get(pc) is Some(known) else { continue }
        let (reads, writes) = pure_at(instructions, idle, pc).unwrap()
        let out = if (reads & known) == reads { known | writes } else { known & (ALL_STATE ^ writes) }
        for next in successors(instructions[pc], facts) {
          if next == start || !inside.contains(next) { continue }
          let merged = match determined.get(next) { Some(d) => d & out; None => out }
          if determined.get(next) != Some(merged) {
            determined[next] = merged
            changed = true
          }
        }
      }
    }
    let decided = body.iter().all(fn(pc) {
      let inst = instructions[pc]
      if inst.mode != "rel" || condition(inst.op, facts[pc]) is Some(_) { return true }
      let (reads, _) = pure_at(instructions, idle, pc).unwrap()
      (reads & determined.get(pc).unwrap_or(0)) == reads
    })
    // the start is reached back by a jump or a branch
    let back = body.filter(fn(pc) {
      let inst = instructions[pc]
      (inst.op == JMP || inst.mode == "rel") && successors(inst, facts).contains(start)
    })
    if !decided || back.is_empty() { continue }
    for pc in back { waits[pc] = start }
    for pc in body { claimed.add(pc) }
  }
  waits
}
