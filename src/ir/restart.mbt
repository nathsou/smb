// Restarts: replacing the stack pointer outside the reset routine.
//
// Some games restart from deep in their calls (after a game over, say):
// `ldx #$ff; txs`, then on with the reset routine's code, abandoning every
// call in progress. The stack analysis finds where (the image's
// `restarts`), and the code from each such TXS on is a routine of its own,
// which the reset routine reaches as it runs. Elsewhere, reaching it unwinds
// the C calls instead, down to the reset routine, which calls it again:
//
//   cpu_restart(k)       the restart routine k (from 1) is pending: the
//                        caller returns at once (`cpu_restarting`);
//   cpu_restart_done()   the reset routine took it.
//
// After a call that can restart, a function returns when
// `cpu_restarting` is set; the reset routine instead calls the pending
// restart routine. The NMI handler can restart too (a soft reset): the
// runtime then drops the suspended foreground and enters the reset routine,
// which takes the restart pending. Registers, flags and the guest stack are
// machine state: the restart routine starts with them as the code left
// them.

// The functions that can reach a restart, other than the reset routine.
fn Program::restarting(self : Program) -> Set[String] {
  let found : Set[String] = Set([])
  let mut changed = true
  while changed {
    changed = false
    for f in self.functions {
      if f.native || f.helper || f.name == self.reset || found.contains(f.name) { continue }
      let reaches = fn(g : String) { self.restarts.contains(g) || g == self.reset || found.contains(g) }
      let calls = f.blocks.iter().any(fn(block) {
        block.stmts.iter().any(fn(s) { s.callee() is Some(g) && reaches(g) }) ||
        block.term.tail_calls().iter().any(reaches)
      })
      if calls {
        found.add(f.name)
        changed = true
      }
    }
  }
  found
}

// Rewrites calls to restart routines outside the reset routine into
// restarts, and every call that can restart to unwind (see above). Run
// after `build`, before `make_resumable`.
pub fn Program::make_restartable(self : Program) -> Unit raise BuildError {
  // (a jump to the reset routine's entry from elsewhere restarts it too:
  // a soft reset)
  let to_reset = self.functions.iter().any(fn(f) {
    f.name != self.reset && !f.native && !f.helper &&
    f.blocks.iter().any(fn(block) {
      block.stmts.iter().any(fn(s) { s.callee() == Some(self.reset) }) ||
      block.term.tail_calls().contains(self.reset)
    })
  })
  if self.restarts.is_empty() && !to_reset { return }
  let restarting = self.restarting()
  // (the NMI handler can: the runtime then runs the reset routine's
  // restart, its suspended foreground dropped)
  match self.irq {
    Some(name) if name != self.reset && restarting.contains(name) =>
      raise BuildError("The IRQ handler can restart the reset routine: \{name}")
    _ => ()
  }
  // the restart routines, the reset routine last
  let routines = self.restarts.copy()
  if to_reset { routines.push(self.reset) }
  let number = fn(g : String) { routines.search(g).unwrap() + 1 }
  for f in self.functions {
    let is_reset = f.name == self.reset
    if !is_reset && !restarting.contains(f.name) { continue }
    let returns = f.block([], Return)
    // the reset routine: on to the restart routine pending
    let restart = if is_reset {
      let start = f.entry
      let calls = []
      let cases = routines.mapi(fn(k, g) {
        // (the reset routine itself: from its start)
        if g == self.reset {
          return (k + 1, f.block([Effect("cpu_restart_done", [])], Goto(start)))
        }
        let again = f.block([Call(g)], Return)
        calls.push(again)
        (k + 1, f.block([Effect("cpu_restart_done", [])], Goto(again)))
      })
      let traps = f.block([], Trap(0))
      let pending = f.block([], Switch(Runtime("cpu_restarting"), cases, traps))
      // and after each, as after any call that can restart
      for again in calls {
        f.blocks[again].term = Branch(Runtime("cpu_restarting"), pending, returns)
      }
      // and on entry, when the NMI handler restarted the game
      f.entry = f.block([], Branch(Runtime("cpu_restarting"), pending, f.entry))
      pending
    } else { returns }
    let may_restart = fn(g : String) { restarting.contains(g) || (is_reset && self.restarts.contains(g)) }
    let restarts_at = fn(g : String) { self.restarts.contains(g) || g == self.reset }
    let work = Array::makei(f.blocks.length(), fn(i) { i })
    while work.pop() is Some(b) {
      let block = f.blocks[b]
      let mut split : Int? = None
      for i, s in block.stmts {
        if s is Call(g) && (may_restart(g) || (!is_reset && restarts_at(g))) {
          split = Some(i)
          break
        }
      }
      if split is Some(i) {
        guard block.stmts[i] is Call(g) else { continue }
        if !is_reset && restarts_at(g) {
          // the restart itself: nothing after it runs here
          block.stmts = block.stmts[:i].to_owned()
          block.stmts.push(Effect("cpu_restart", [Const(number(g))]))
          block.term = Return
          continue
        }
        let rest = f.block(block.stmts[i + 1:].to_owned(), block.term)
        block.stmts = block.stmts[:i + 1].to_owned()
        block.term = Branch(Runtime("cpu_restarting"), restart, rest)
        work.push(rest)
        continue
      }
      match block.term {
        TailCall(g) if !is_reset && restarts_at(g) => {
          block.stmts.push(Effect("cpu_restart", [Const(number(g))]))
          block.term = Return
        }
        // the reset routine keeps its frame: it calls, and goes on
        TailCall(g) if is_reset && may_restart(g) => {
          block.stmts.push(Call(g))
          block.term = Branch(Runtime("cpu_restarting"), restart, returns)
        }
        Dispatch(_, _, cases) | Jump(_, cases) if cases.iter().any(fn(c) { restarts_at(c.1) }) =>
          raise BuildError("A computed jump in \{f.name} can restart the reset routine")
        Dispatch(_, _, cases) | Jump(_, cases) if is_reset && cases.iter().any(fn(c) { may_restart(c.1) }) =>
          raise BuildError("A computed jump in the reset routine can restart it")
        _ => ()
      }
    }
  }
}
