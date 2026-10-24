// Resumable functions: suspending in a wait loop.
//
// A wait loop (see the image's loops.mbt) waits for the next interrupt where
// it comes back to its start: the frame ends there, and the runtime delivers
// the interrupt before the next one. The loop can sit deep in calls, while
// the runtime only regains control when the reset routine returns. So the
// functions that can reach a wait suspend instead: each records where to
// resume and returns, down to the reset routine; the next frame calls the
// reset routine again, and each function on the way goes straight back to
// where it was, calling its callee again until the innermost continues after
// its wait. Registers and flags are machine state, so they survive; the
// guest stack too.
//
// The runtime keeps the stack of resume points:
//
//   cpu_suspend(point)   push a resume point and mark the foreground
//                        suspended (`cpu_suspended`);
//   cpu_resume_depth     the points pending, innermost first pushed;
//   cpu_resume_pop()     pop the outermost pending point into
//                        `cpu_resume_point`.
//
// A suspending function starts by resuming when points are pending:
//
//   entry: if (cpu_resume_depth != 0) { cpu_resume_pop();
//            switch (cpu_resume_point) { case k: goto resume_k; ... } }
//
// where a resume point is the block of a call to a suspending function (the
// call is made again, and resumes the callee), or the start of a wait loop.
// After such a call, `if (cpu_suspended) { cpu_suspend(k); return; }`.
// Tail calls and dispatches to suspending routines become calls followed by
// returns, so that they can be resumed.

// Whether each function can reach a wait loop.
fn Program::suspending(self : Program) -> Set[String] {
  let found : Set[String] = Set([])
  let mut changed = true
  while changed {
    changed = false
    for f in self.functions {
      if f.native || f.helper || found.contains(f.name) { continue }
      let reaches = f.blocks.iter().any(fn(block) {
        block.term is Suspend(_) ||
        block.stmts.iter().any(fn(s) { s.callee() is Some(g) && found.contains(g) }) ||
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

// Moves blocks to the front, in this order (the backends lay blocks out in
// order): the entry and its resume dispatch read first.
fn Function::move_to_front(self : Function, first : Array[Int]) -> Unit {
  let order = first.copy()
  for b in 0..<self.blocks.length() {
    if !first.contains(b) { order.push(b) }
  }
  let index = Array::make(self.blocks.length(), 0)
  for i, b in order { index[b] = i }
  self.blocks = order.map(fn(b) { self.blocks[b] })
  for block in self.blocks { block.term = block.term.map_targets(fn(t) { index[t] }) }
  self.entry = index[self.entry]
}

fn Function::block(self : Function, stmts : Array[Stmt], term : Term) -> Int {
  self.blocks.push({ label: "", stmts, term })
  self.blocks.length() - 1
}

// Rewrites the functions that can reach a wait loop to suspend and resume
// (see above). Run once, after `build` and before any analysis.
pub fn Program::make_resumable(self : Program) -> Unit raise BuildError {
  let suspending = self.suspending()
  if suspending.contains(self.nmi) {
    // the chain of calls to a wait, for the message
    let chain = [self.nmi]
    while self.function(chain[chain.length() - 1]) is Some(f) && chain.length() < 32 {
      let mut next : String? = None
      for block in f.blocks {
        for s in block.stmts {
          if s.callee() is Some(g) && suspending.contains(g) && next is None { next = Some(g) }
        }
        for g in block.term.tail_calls() {
          if suspending.contains(g) && next is None { next = Some(g) }
        }
      }
      match next {
        Some(g) => chain.push(g)
        None => break
      }
    }
    raise BuildError("The interrupt handler reaches a wait loop: \{chain.join(" -> ")}")
  }
  for f in self.functions {
    if !suspending.contains(f.name) { continue }
    let points : Array[Int] = [] // resume point -> block
    let returns = f.block([], Return)
    let traps = f.block([], Trap(0))
    // `if (cpu_suspended) { cpu_suspend(k); return; }` else `after`,
    // resuming at `resume`
    fn check(again : Int, after : Int) -> Term {
      let suspend = f.block([Effect("cpu_suspend", [Const(points.length())])], Return)
      points.push(again)
      Branch(Runtime("cpu_suspended"), suspend, after)
    }
    // A call to `g` then `after`, as a block that resumes by calling again.
    fn call(g : String, after : Int) -> Int {
      if !suspending.contains(g) { return f.block([Call(g)], Goto(after)) }
      let at = f.block([Call(g)], Return)
      f.blocks[at].term = check(at, after)
      at
    }
    // the function's blocks, and the rest of each block split after a call
    let work = Array::makei(f.blocks.length() - 2, fn(i) { i })
    while work.pop() is Some(b) {
      let block = f.blocks[b]
      let mut split : Int? = None
      for i, s in block.stmts {
        if s is Call(g) && suspending.contains(g) {
          split = Some(i)
          break
        }
      }
      if split is Some(i) {
        guard block.stmts[i] is Call(g) else { continue }
        let rest = f.block(block.stmts[i + 1:].to_owned(), block.term)
        block.stmts = block.stmts[:i].to_owned()
        block.term = Goto(call(g, rest))
        work.push(rest)
        continue
      }
      match block.term {
        Suspend(start) => {
          let suspend = f.block([Effect("cpu_suspend", [Const(points.length())])], Return)
          points.push(start)
          block.term = Goto(suspend)
        }
        TailCall(g) if suspending.contains(g) => block.term = Goto(call(g, returns))
        Dispatch(helper, return_address, cases) if cases.iter().any(fn(c) { suspending.contains(c.1) }) => {
          let t = f.new_temp("target", U16)
          block.stmts.push(CallValue(t, helper, return_address))
          block.term = Switch(Temp(t), cases.map(fn(c) { (c.0, call(c.1, returns)) }), traps)
        }
        Jump(target, cases) if cases.iter().any(fn(c) { suspending.contains(c.1) }) =>
          block.term = Switch(target, cases.map(fn(c) { (c.0, call(c.1, returns)) }), traps)
        _ => ()
      }
    }
    if points.is_empty() { continue }
    let resuming = f.block([Effect("cpu_resume_pop", [])], Switch(Runtime("cpu_resume_point"), points.mapi(fn(k, at) { (k, at) }), traps))
    f.entry = f.block([], Branch(Binary(Ne, Runtime("cpu_resume_depth"), Const(0)), resuming, f.entry))
    f.move_to_front([f.entry, resuming])
  }
}
