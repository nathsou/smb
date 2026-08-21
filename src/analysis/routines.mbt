pub(all) struct RoutineView {
  entry: Int
  instructions: Map[Int, DecodedInstruction]
} derive(Debug)

pub fn direct_entries(cfg: ControlFlowGraph, vector_entries: Array[Int]) -> Array[Int] {
  let entries = vector_entries.copy()
  for instruction in cfg.instructions.values() {
    match instruction.flow {
      Call(target~, ..) => {
        if !entries.contains(target) {
          entries.push(target)
        }
      }
      _ => ()
    }
  }
  entries.sort()
  entries
}

fn routine_successors(instruction: DecodedInstruction) -> Array[Int] {
  match instruction.flow {
    // A direct call is emitted as a C call. Its continuation remains in the
    // current view, while the callee gets its own view.
    Call(fallthrough~, ..) => [fallthrough]
    Fallthrough(next) => [next]
    Branch(target~, fallthrough~) => [target, fallthrough]
    Jump(target) => [target]
    Return | IndirectJump(_) | Stop => []
  }
}

// Crucially, `seen` is local to one entry. A shared 6502 block therefore
// appears in every relevant view rather than being claimed by the first C
// function that reaches it.
pub fn recover_routine_view(cfg: ControlFlowGraph, entry: Int) -> RoutineView {
  let worklist = [entry]
  let instructions: Map[Int, DecodedInstruction] = {}

  while !worklist.is_empty() {
    let address = worklist.pop().unwrap()
    if !instructions.contains(address) {
      match cfg.instructions.get(address) {
        Some(instruction) => {
          instructions[address] = instruction
          for successor in routine_successors(instruction) {
            worklist.push(successor)
          }
        }
        // An unresolved indirect or malformed target is intentionally absent
        // here and is handled by the PC dispatcher fallback.
        None => ()
      }
    }
  }

  { entry, instructions }
}

pub fn recover_routine_views(cfg: ControlFlowGraph, entries: Array[Int]) -> Array[RoutineView] {
  entries.map(fn(entry) { recover_routine_view(cfg, entry) })
}
