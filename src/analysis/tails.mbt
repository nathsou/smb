pub(all) struct TailAnalysis {
  tail_jumps: Set[Int]
  callable_fallthroughs: Set[Int]
} derive(Debug)

// A JMP never creates a 6502 stack frame. It is therefore represented as a
// transfer within the current routine view; C emission may later choose a
// tail call only when that is profitable. This avoids source-level JSR/RTS
// wrappers around shared fragments.
pub fn analyze_tails(cfg: ControlFlowGraph, entries: Array[Int]) -> TailAnalysis {
  let tail_jumps = Set([])
  let callable_fallthroughs = Set([])
  let entry_set = Set(entries)

  for instruction in cfg.instructions.values() {
    match instruction.flow {
      Jump(_) => tail_jumps.add(instruction.address)
      Fallthrough(next) if entry_set.contains(next) => callable_fallthroughs.add(instruction.address)
      _ => ()
    }
  }

  { tail_jumps, callable_fallthroughs }
}

pub fn is_tail_jump(analysis: TailAnalysis, address: Int) -> Bool {
  analysis.tail_jumps.contains(address)
}
