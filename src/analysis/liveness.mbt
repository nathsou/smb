pub let flag_carry = 1
pub let flag_zero = 2
pub let flag_negative = 4
pub let flag_overflow = 8

pub(all) struct FlagLiveness {
  live_in: Map[Int, Int]
  live_out: Map[Int, Int]
} derive(Debug)

fn flag_uses(instruction: DecodedInstruction) -> Int {
  match instruction.opcode {
    0x10 | 0x30 => flag_negative
    0x50 | 0x70 => flag_overflow
    0x90 | 0xb0 => flag_carry
    0xd0 | 0xf0 => flag_zero
    0x69 | 0x65 | 0x75 | 0x6d | 0x7d | 0x79 | 0x61 | 0x71 | 0xe9 | 0xe5 | 0xf5 | 0xed | 0xfd | 0xf9 | 0xe1 | 0xf1 => flag_carry
    0x2a | 0x26 | 0x2e | 0x36 | 0x3e | 0x6a | 0x66 | 0x6e | 0x76 | 0x7e => flag_carry
    _ => 0
  }
}

fn flag_defs(instruction: DecodedInstruction) -> Int {
  match instruction.opcode {
    0x18 | 0x38 => flag_carry
    0xb8 => flag_overflow
    0x24 | 0x2c => flag_zero | flag_negative | flag_overflow
    0xc0 | 0xc4 | 0xcc | 0xc9 | 0xc5 | 0xcd | 0xd5 | 0xdd | 0xd9 | 0xc1 | 0xd1 | 0xe0 | 0xe4 | 0xec => flag_carry | flag_zero | flag_negative
    0x69 | 0x65 | 0x75 | 0x6d | 0x7d | 0x79 | 0x61 | 0x71 | 0xe9 | 0xe5 | 0xf5 | 0xed | 0xfd | 0xf9 | 0xe1 | 0xf1 => flag_carry | flag_zero | flag_negative | flag_overflow
    0x0a | 0x06 | 0x0e | 0x16 | 0x1e | 0x2a | 0x26 | 0x2e | 0x36 | 0x3e | 0x4a | 0x46 | 0x4e | 0x56 | 0x5e | 0x6a | 0x66 | 0x6e | 0x76 | 0x7e => flag_carry | flag_zero | flag_negative
    0x09 | 0x05 | 0x15 | 0x0d | 0x1d | 0x19 | 0x01 | 0x11 | 0x29 | 0x25 | 0x35 | 0x2d | 0x3d | 0x39 | 0x21 | 0x31 | 0x49 | 0x45 | 0x4d | 0xa0 | 0xa2 | 0xa4 | 0xa5 | 0xa6 | 0xa9 | 0xac | 0xad | 0xae | 0xb1 | 0xb4 | 0xb5 | 0xb6 | 0xb9 | 0xbc | 0xbd | 0xbe | 0x88 | 0xc8 | 0xca | 0xe8 => flag_zero | flag_negative
    _ => 0
  }
}

pub fn analyze_flag_liveness(cfg: ControlFlowGraph) -> FlagLiveness {
  let live_in: Map[Int, Int] = {}
  let live_out: Map[Int, Int] = {}
  let mut changed = true

  while changed {
    changed = false
    for instruction in cfg.instructions.values() {
      let mut out = 0
      for successor in successors(instruction) {
        out = out | live_in.get(successor).unwrap_or(0)
      }
      let in_ = flag_uses(instruction) | (out & (0x0f ^ flag_defs(instruction)))
      if live_out.get(instruction.address).unwrap_or(-1) != out {
        live_out[instruction.address] = out
        changed = true
      }
      if live_in.get(instruction.address).unwrap_or(-1) != in_ {
        live_in[instruction.address] = in_
        changed = true
      }
    }
  }

  { live_in, live_out }
}

pub fn can_elide_bit(liveness: FlagLiveness, instruction: DecodedInstruction) -> Bool {
  instruction.opcode == 0x2c &&
  (liveness.live_out.get(instruction.address).unwrap_or(0) & (flag_zero | flag_negative | flag_overflow)) == 0
}
