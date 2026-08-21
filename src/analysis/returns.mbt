pub(all) enum LoweredTransfer {
  Continue(Int)
  Conditional(target~: Int, fallthrough~: Int)
  TailTransfer(Int)
  Call(target~: Int, continuation~: Int)
  ReturnToCaller
  NeedsDispatcher
  Halt
} derive(Debug)

// Return interpretation is local to a routine view.  This is the replacement
// for the former source-level `__ret__` pseudo-label: any reachable RTS/RTI
// terminates the active call context without requiring a named shared block.
pub fn lower_transfer(instruction: DecodedInstruction) -> LoweredTransfer {
  match instruction.flow {
    Fallthrough(next) => Continue(next)
    Branch(target~, fallthrough~) => Conditional(target~, fallthrough~)
    Jump(target) => TailTransfer(target)
    Call(target~, fallthrough~) => Call(target~, continuation=fallthrough)
    Return => ReturnToCaller
    IndirectJump(_) => NeedsDispatcher
    Stop => Halt
  }
}

pub fn RoutineView::returns(self: RoutineView) -> Array[Int] {
  let returns = []
  for instruction in self.instructions.values() {
    match lower_transfer(instruction) {
      ReturnToCaller => returns.push(instruction.address)
      _ => ()
    }
  }
  returns.sort()
  returns
}
