pub(all) struct StackSummary {
  pops_return_address: Bool
  unbalanced_return: Bool
} derive(Debug)

priv struct StackWork {
  address: Int
  depth: Int
}

fn stack_delta(opcode: Int) -> Int {
  match opcode {
    0x08 | 0x48 => 1 // PHP, PHA
    0x28 | 0x68 => -1 // PLP, PLA
    _ => 0
  }
}

fn stack_successors(instruction: DecodedInstruction) -> Array[Int] {
  match instruction.flow {
    Call(fallthrough~, ..) | Fallthrough(fallthrough) => [fallthrough]
    Branch(target~, fallthrough~) => [target, fallthrough]
    Jump(target) => [target]
    Return | IndirectJump(_) | Stop => []
  }
}

// The summary starts immediately after a normal JSR has installed its return
// token. A PLA/PLP at depth zero consequently consumes that token instead of
// user stack data. JumpEngine is the important SMB example.
pub fn summarize_stack(view: RoutineView) -> StackSummary {
  let worklist: Array[StackWork] = [{ address: view.entry, depth: 0 }]
  let depths: Map[Int, Int] = {}
  let mut pops_return_address = false
  let mut unbalanced_return = false

  while !worklist.is_empty() {
    let work = worklist.pop().unwrap()
    match depths.get(work.address) {
      Some(previous) if previous == work.depth => ()
      _ => {
        depths[work.address] = work.depth
        match view.instructions.get(work.address) {
          Some(instruction) => {
            if (instruction.opcode == 0x28 || instruction.opcode == 0x68) && work.depth <= 0 {
              pops_return_address = true
            }
            match instruction.flow {
              Return => {
                if work.depth != 0 {
                  unbalanced_return = true
                }
              }
              _ => {
                let next_depth = work.depth + stack_delta(instruction.opcode)
                for address in stack_successors(instruction) {
                  worklist.push({ address, depth: next_depth })
                }
              }
            }
          }
          None => ()
        }
      }
    }
  }

  { pops_return_address, unbalanced_return }
}

pub fn requires_pc_dispatch(summary: StackSummary) -> Bool {
  summary.pops_return_address || summary.unbalanced_return
}
