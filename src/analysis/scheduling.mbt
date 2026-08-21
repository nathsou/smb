pub(all) enum SchedulerAction { YieldUntilInterrupt; ContinueExecuting } derive(Debug, Eq)

// A reset handler often ends in an intentional idle spin after enabling NMI.
// Lowering it to an explicit scheduler yield preserves the host ABI without
// pretending that a reset routine ends with RTI.
pub fn scheduler_action(instruction: DecodedInstruction, is_reset_path: Bool) -> SchedulerAction {
  match instruction.flow {
    Jump(target) if is_reset_path && target == instruction.address => YieldUntilInterrupt
    _ => ContinueExecuting
  }
}

pub fn reset_idle_loops(cfg: ControlFlowGraph, reset_entry: Int) -> Set[Int] {
  let idle_loops = Set([])
  let reachable = Set([])
  let worklist = [reset_entry]
  while !worklist.is_empty() {
    let address = worklist.pop().unwrap()
    if !reachable.contains(address) {
      reachable.add(address)
      match cfg.instructions.get(address) {
        Some(instruction) => {
          if scheduler_action(instruction, true) == YieldUntilInterrupt {
            idle_loops.add(address)
          }
          for successor in successors(instruction) {
            worklist.push(successor)
          }
        }
        None => ()
      }
    }
  }
  idle_loops
}
