// Preemption: the interrupt between two frames' worth of foreground.
//
// The frame model delivers the interrupt where the foreground waits for it
// (a wait loop, `jmp *`). A game that computes for frames on end in the
// foreground without waiting (a chess engine thinking) is interrupted by
// the hardware wherever it is; here it yields instead, at a transfer back
// (a taken branch or a jump to an address at or before it), once it went
// back `preempt` times in the frame (the map's budget):
//
//   cpu_poll()         count a transfer back; past the budget it sets
//                      `cpu_preempted`, and the foreground suspends there
//                      (as at a wait loop, see escape.mbt) and goes on
//                      after the next interrupt.
//
// The interrupt handlers do not yield: the code they run (and any
// instruction a function of theirs shares) does not poll. The reference
// interpreter polls at the same transfers (`polls`), so both end frames at
// the same places.

// The functions the interrupt handlers reach.
fn Program::interrupt_reach(self : Program) -> Set[String] {
  let found : Set[String] = Set([])
  let work = [self.nmi]
  match self.irq {
    Some(irq) => work.push(irq)
    None => ()
  }
  while work.pop() is Some(name) {
    if found.contains(name) { continue }
    found.add(name)
    guard self.function(name) is Some(f) else { continue }
    for block in f.blocks {
      for s in block.stmts {
        if s.callee() is Some(g) { work.push(g) }
      }
      for g in block.term.tail_calls() { work.push(g) }
    }
  }
  found
}

// Drops the polls the interrupt handlers could reach (by instruction, in
// any function) and returns the locations of those left, ascending. Run
// after `build`, before `make_resumable`.
pub fn Program::make_preemptible(self : Program) -> Array[Int] {
  let reached = self.interrupt_reach()
  let banned : Set[Int] = Set([])
  for f in self.functions {
    if !reached.contains(f.name) { continue }
    for poll in f.polls { banned.add(poll.1) }
  }
  let kept : Set[Int] = Set([])
  for f in self.functions {
    for poll in f.polls {
      let (block, location) = poll
      if banned.contains(location) {
        // straight on to the target (and no suspension left behind)
        guard f.blocks[block].term is Branch(_, suspend, target) else { continue }
        f.blocks[block].stmts = []
        f.blocks[block].term = Goto(target)
        f.blocks[suspend].term = Goto(target)
      } else {
        kept.add(location)
      }
    }
    f.polls.clear()
  }
  let out = kept.to_array()
  out.sort()
  out
}
