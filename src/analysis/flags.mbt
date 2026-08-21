pub(all) enum FlagValue { Clear; KnownSet; Unknown } derive(Debug, Eq)

pub(all) struct FlagState {
  carry: FlagValue
  zero: FlagValue
  negative: FlagValue
  overflow: FlagValue
} derive(Debug, Eq)

pub(all) struct FlagAnalysis {
  before: Map[Int, FlagState]
} derive(Debug)

priv struct FlagWork {
  address: Int
  state: FlagState
}

fn unknown_flags() -> FlagState {
  { carry: Unknown, zero: Unknown, negative: Unknown, overflow: Unknown }
}

fn join_flag(lhs: FlagValue, rhs: FlagValue) -> FlagValue {
  if lhs == rhs { lhs } else { Unknown }
}

fn join_state(lhs: FlagState, rhs: FlagState) -> FlagState {
  {
    carry: join_flag(lhs.carry, rhs.carry),
    zero: join_flag(lhs.zero, rhs.zero),
    negative: join_flag(lhs.negative, rhs.negative),
    overflow: join_flag(lhs.overflow, rhs.overflow),
  }
}

fn nz(value: Int) -> (FlagValue, FlagValue) {
  (
    if value == 0 { KnownSet } else { Clear },
    if (value & 0x80) != 0 { KnownSet } else { Clear },
  )
}

fn updates_nz(opcode: Int) -> Bool {
  match opcode {
    0x09 | 0x0d | 0x11 | 0x15 | 0x19 | 0x1d | 0x21 | 0x25 | 0x29 | 0x2d | 0x31 | 0x35 | 0x39 | 0x3d |
    0x41 | 0x45 | 0x49 | 0x4d | 0x51 | 0x55 | 0x59 | 0x5d | 0x61 | 0x65 | 0x69 | 0x6d | 0x71 | 0x75 | 0x79 | 0x7d |
    0xa0 | 0xa4 | 0xa5 | 0xa6 | 0xa9 | 0xac | 0xad | 0xae | 0xb1 | 0xb4 | 0xb5 | 0xb6 | 0xb9 | 0xbc | 0xbd | 0xbe |
    0xe8 | 0xc8 | 0xca | 0x88 => true
    _ => false
  }
}

fn transfer_flags(image: @layout.Image, instruction: DecodedInstruction, state: FlagState) -> FlagState {
  match instruction.opcode {
    0x18 => { ..state, carry: Clear } // CLC
    0x38 => { ..state, carry: KnownSet } // SEC
    0xb8 => { ..state, overflow: Clear } // CLV
    0x58 | 0x78 | 0xd8 | 0xf8 => state
    0xa0 | 0xa2 | 0xa9 => { // immediate LDY/LDX/LDA
      match image.read_byte(instruction.address + 1) {
        Some(value) => {
          let (zero, negative) = nz(value)
          { ..state, zero, negative }
        }
        None => state
      }
    }
    0x24 | 0x2c => {
      { ..state, zero: Unknown, negative: Unknown, overflow: Unknown }
    }
    _ => {
      if updates_nz(instruction.opcode) {
        { ..state, zero: Unknown, negative: Unknown }
      } else {
        state
      }
    }
  }
}

fn branch_taken(opcode: Int, state: FlagState) -> Bool? {
  match opcode {
    0x10 => match state.negative { Clear => Some(true); KnownSet => Some(false); Unknown => None }
    0x30 => match state.negative { KnownSet => Some(true); Clear => Some(false); Unknown => None }
    0x50 => match state.overflow { Clear => Some(true); KnownSet => Some(false); Unknown => None }
    0x70 => match state.overflow { KnownSet => Some(true); Clear => Some(false); Unknown => None }
    0x90 => match state.carry { Clear => Some(true); KnownSet => Some(false); Unknown => None }
    0xb0 => match state.carry { KnownSet => Some(true); Clear => Some(false); Unknown => None }
    0xd0 => match state.zero { Clear => Some(true); KnownSet => Some(false); Unknown => None }
    0xf0 => match state.zero { KnownSet => Some(true); Clear => Some(false); Unknown => None }
    _ => None
  }
}

fn next_addresses(instruction: DecodedInstruction, state: FlagState) -> Array[Int] {
  match instruction.flow {
    Branch(target~, fallthrough~) => match branch_taken(instruction.opcode, state) {
      Some(true) => [target]
      Some(false) => [fallthrough]
      None => [target, fallthrough]
    }
    Call(fallthrough~, ..) | Fallthrough(fallthrough) => [fallthrough]
    Jump(target) => [target]
    Return | IndirectJump(_) | Stop => []
  }
}

pub fn analyze_flags(image: @layout.Image, cfg: ControlFlowGraph) -> FlagAnalysis {
  let before: Map[Int, FlagState] = {}
  let worklist = cfg.roots.map(fn(address) { { address, state: unknown_flags() } })

  while !worklist.is_empty() {
    let work = worklist.pop().unwrap()
    let state = match before.get(work.address) {
      Some(previous) => join_state(previous, work.state)
      None => work.state
    }
    match before.get(work.address) {
      Some(previous) if previous == state => ()
      _ => {
        before[work.address] = state
        match cfg.instructions.get(work.address) {
          Some(instruction) => {
            let after = transfer_flags(image, instruction, state)
            for address in next_addresses(instruction, after) {
              worklist.push({ address, state: after })
            }
          }
          None => ()
        }
      }
    }
  }

  { before }
}
