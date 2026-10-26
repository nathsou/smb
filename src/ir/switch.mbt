// Coroutines: stacks the program switches between.
//
// A program can run coroutines, each on a stack of its own (the map's
// `thread`): to switch, it saves the stack pointer and loads the other's
// (`tsx / stx saved / ldx other / txs`), pulls what the other pushed before
// it switched, and returns where the other called the switch from. With
// exact returns that return goes to an address no call of this coroutine
// waits for. The first function whose call sees it is the one whose return
// address the switch left on its own stack: it suspends there, waiting for
// that address (as at a wait loop, see escape.mbt), and the functions down
// to the reset routine suspend too. The runtime then keeps the coroutine's
// resume points and resumes the coroutine that waits for the address the
// switch returned to (or starts one), from the reset routine again:
//
//   cpu_switch()          at the switch's TXS: a switch is under way
//                         (`cpu_switching` 1);
//   cpu_await(back)       the function whose call was switched away from
//                         waits for `back` (and `cpu_switching` becomes 2);
//   cpu_start_thread(k)   a jump to coroutine k's start, on its new stack:
//                         a switch too, to a coroutine that starts;
//   cpu_thread            the coroutine running, which the reset routine
//                         calls (0: its own code).
//
// Resuming, the function that waited continues after its call, where the
// return address is checked again: the other coroutine's switch returned to
// it.

// The functions that can reach a switch.
fn Program::switching(self : Program) -> Set[String] {
  let found : Set[String] = Set([])
  let mut changed = true
  while changed {
    changed = false
    for f in self.functions {
      if f.native || f.helper || found.contains(f.name) { continue }
      let reaches = f.blocks.iter().any(fn(block) {
        block.stmts.iter().any(fn(s) {
          s is Effect("cpu_switch" | "cpu_start_thread", _) || (s.callee() is Some(g) && found.contains(g))
        }) ||
        block.term.tail_calls().iter().any(fn(g) { found.contains(g) })
      })
      if reaches {
        found.add(f.name)
        changed = true
      }
    }
  }
  found
}

// Rewrites the program to switch between coroutines (see above). Run after
// `build`, before `make_restartable`.
pub fn Program::make_switchable(self : Program) -> Unit raise BuildError {
  if self.threads.is_empty() { return }
  // a jump to a coroutine's start starts it
  for f in self.functions {
    for block in f.blocks {
      match block.term {
        TailCall(g) if self.threads.contains(g) => {
          block.stmts.push(Effect("cpu_start_thread", [Const(self.threads.search(g).unwrap() + 1)]))
          block.term = Return
        }
        _ =>
          if block.stmts.iter().any(fn(s) { s.callee() is Some(g) && self.threads.contains(g) }) {
            raise BuildError("A call to a coroutine's start in \{f.name}")
          }
      }
    }
  }
  let switching = self.switching()
  // after a call that can switch, the check of where it returned waits
  // there while a switch is under way
  for f in self.functions {
    if !switching.contains(f.name) { continue }
    for b in 0..<f.blocks.length() {
      let block = f.blocks[b]
      guard block.term is Branch(Binary(Ne, Runtime("cpu_return"), Const(back)) as returned, elsewhere, after) &&
        block.stmts.last() is Some(Call(g)) && switching.contains(g) else { continue }
      let check = f.block([], block.term)
      block.term = Goto(check)
      let wait = f.block([Effect("cpu_await", [Const(back)])], Suspend(check))
      let switched = f.block([], Branch(Binary(Eq, Runtime("cpu_switching"), Const(1)), wait, elsewhere))
      f.blocks[check].term = Branch(returned, switched, after)
    }
  }
  // the reset routine runs the coroutine the runtime chose
  guard self.function(self.reset) is Some(reset) else { return }
  let traps = reset.block([], Trap(0))
  let cases = self.threads.mapi(fn(k, g) { (k + 1, reset.block([Call(g)], Return)) })
  let dispatch = reset.block([], Switch(Runtime("cpu_thread"), cases, traps))
  reset.entry = reset.block([], Branch(Binary(Ne, Runtime("cpu_thread"), Const(0)), dispatch, reset.entry))
}
