pub(all) struct DispatcherPlan {
  addresses: Set[Int]
} derive(Debug)

// Isolate only the code that cannot safely use the structured C path.  This
// keeps ordinary direct-call routines statically compiled while preserving a
// sound fallback for stack tricks and unresolved indirect control flow.
pub fn plan_dispatcher(cfg: ControlFlowGraph, views: Array[RoutineView]) -> DispatcherPlan {
  let addresses = Set([])
  for instruction in cfg.instructions.values() {
    match instruction.flow {
      IndirectJump(_) => addresses.add(instruction.address)
      _ => ()
    }
  }
  for view in views {
    if requires_pc_dispatch(summarize_stack(view)) {
      for address in view.instructions.keys() {
        addresses.add(address)
      }
    }
  }
  { addresses }
}

pub fn DispatcherPlan::requires_dispatch(self: DispatcherPlan, address: Int) -> Bool {
  self.addresses.contains(address)
}
