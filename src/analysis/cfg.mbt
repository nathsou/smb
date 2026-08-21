// Byte-addressed decoding and recursive-descent control-flow recovery.
// Nodes are keyed by address, so overlapping encodings are intentionally kept.

pub(all) enum Flow {
  Fallthrough(Int)
  Branch(target~: Int, fallthrough~: Int)
  Jump(Int)
  Call(target~: Int, fallthrough~: Int)
  Return
  IndirectJump(Int)
  Stop
} derive(Debug)

pub(all) struct DecodedInstruction {
  address: Int
  opcode: Int
  size: Int
  flow: Flow
} derive(Debug)

pub(all) struct ControlFlowGraph {
  roots: Array[Int]
  instructions: Map[Int, DecodedInstruction]
} derive(Debug)

pub suberror DecodeError {
  MissingByte(Int)
  UnsupportedOpcode(Int)
} derive(Debug)

fn byte(image: @layout.Image, address: Int) -> Int raise DecodeError {
  image.read_byte(address).unwrap_or_error(MissingByte(address))
}

fn word(image: @layout.Image, address: Int) -> Int raise DecodeError {
  image.read_word(address).unwrap_or_error(MissingByte(address))
}

fn branch_target(image: @layout.Image, address: Int) -> Int raise DecodeError {
  let offset = byte(image, address + 1)
  let signed_offset = if offset >= 0x80 { offset - 0x100 } else { offset }
  address + 2 + signed_offset
}

fn is_branch(opcode: Int) -> Bool {
  match opcode {
    0x10 | 0x30 | 0x50 | 0x70 | 0x90 | 0xb0 | 0xd0 | 0xf0 => true
    _ => false
  }
}

// Size decoding covers the official 6502 instruction set.  Unsupported bytes
// are kept explicit instead of silently being treated as data.
fn instruction_size(opcode: Int) -> Int? {
  if is_branch(opcode) {
    return Some(2)
  }

  match opcode {
    0x00 => Some(1)
    0x08 | 0x18 | 0x28 | 0x38 | 0x40 | 0x48 | 0x58 | 0x60 | 0x68 | 0x78 | 0x88 | 0x8a | 0x98 | 0x9a | 0xa8 | 0xaa | 0xb8 | 0xba | 0xc8 | 0xca | 0xd8 | 0xe8 | 0xea | 0xf8 => Some(1)
    0x20 | 0x2c | 0x4c | 0x6c | 0x8c | 0x8d | 0x8e | 0xac | 0xad | 0xae | 0xbc | 0xbd | 0xbe | 0xcc | 0xcd | 0xce | 0xec | 0xed | 0xee | 0xfe | 0xde | 0xdd | 0xdc => Some(3)
    0x24 | 0x84 | 0x85 | 0x86 | 0x94 | 0x95 | 0x96 | 0xa0 | 0xa4 | 0xa6 | 0xb4 | 0xb6 | 0xc0 | 0xc4 | 0xd4 | 0xe0 | 0xe4 | 0xf4 => Some(2)
    _ => {
      let cc = opcode & 0x03
      let bbb = (opcode >> 2) & 0x07
      match cc {
        1 => match bbb {
          0 | 1 | 2 | 4 | 5 => Some(2)
          3 | 6 | 7 => Some(3)
          _ => None
        }
        2 => match bbb {
          0 | 1 | 4 | 5 => Some(2)
          2 => Some(1)
          3 | 6 | 7 => Some(3)
          _ => None
        }
        _ => None
      }
    }
  }
}

pub fn decode(image: @layout.Image, address: Int) -> DecodedInstruction raise DecodeError {
  let opcode = byte(image, address)
  let size = instruction_size(opcode).unwrap_or_error(UnsupportedOpcode(opcode))
  let next = address + size
  let flow = match opcode {
    0x20 => Flow::Call(target=word(image, address + 1), fallthrough=next)
    0x4c => Jump(word(image, address + 1))
    0x6c => IndirectJump(word(image, address + 1))
    0x60 | 0x40 => Return
    0x00 => Stop
    _ => {
      if is_branch(opcode) {
        Branch(target=branch_target(image, address), fallthrough=next)
      } else {
        Fallthrough(next)
      }
    }
  }
  { address, opcode, size, flow }
}

fn enqueue_successors(instruction: DecodedInstruction, worklist: Array[Int]) -> Unit {
  match instruction.flow {
    Fallthrough(next) => worklist.push(next)
    Branch(target~, fallthrough~) => { worklist.push(target); worklist.push(fallthrough) }
    Jump(target) => worklist.push(target)
    Call(target~, fallthrough~) => { worklist.push(target); worklist.push(fallthrough) }
    Return | IndirectJump(_) | Stop => ()
  }
}

pub fn recover_cfg(image: @layout.Image, roots: Array[Int]) -> ControlFlowGraph raise DecodeError {
  let worklist = roots.copy()
  let instructions: Map[Int, DecodedInstruction] = {}

  while !worklist.is_empty() {
    let address = worklist.pop().unwrap()
    if !instructions.contains(address) {
      let instruction = decode(image, address)
      instructions[address] = instruction
      enqueue_successors(instruction, worklist)
    }
  }

  { roots, instructions }
}

pub fn successors(instruction: DecodedInstruction) -> Array[Int] {
  match instruction.flow {
    Fallthrough(next) => [next]
    Branch(target~, fallthrough~) => [target, fallthrough]
    Jump(target) => [target]
    Call(target~, fallthrough~) => [target, fallthrough]
    Return | IndirectJump(_) | Stop => []
  }
}
