// Check the contract used by the function backend. Depth counts data above a
// normal return token. A call is balanced only after its callee has also been
// checked. Nonstandard return manipulation is diagnosed, not silently mapped
// to a C return. The inline-dispatch proof is its own explicit exception.
//
// Programs that read or drop return addresses (`pla; pla` with nothing
// pushed, then a return or a jump) keep them: their calls push the return
// address on the guest stack as JSR does and their returns pull it, so those
// accesses see the machine's bytes, and a return goes wherever the address
// pulled says (see `exact`, and the IR's `exact` returns). Depths below the
// return token are then allowed (bounded at -16, below which the analysis
// does not distinguish them).
pub fn Program::validate_stacks(self: Program) -> Unit raise ImageError {
  // The same address can be reached under more than one machine contract.
  // Keep those contexts separate: being an interrupt vector must not make an
  // ordinary JSR to that address eligible to return with RTI.
  // (and the data a jump into a window leaves on the stack for the routine
  // it continues in: an interrupt handler's saved registers, say)
  let entries: Array[(Int, Int, Int)] = []
  fn add_entry(entry: Int, kind: Int, depth: Int) {
    if !entries.contains((entry, kind, depth)) { entries.push((entry, kind, depth)) }
  }
  for i, root in self.roots {
    add_entry(self.image.symbols[root], if i == 0 { 0 } else { 1 }, 0)
  }
  // A call into the window from the fixed bank calls whichever routine is
  // mapped there; a computed jump continues in its target.
  fn callees(target: Int) -> Array[Int] {
    match self.windows.get(target) { Some(banked) => banked; None => [target] }
  }
  for inst in self.instructions.values() {
    if inst.op == JSR {
      if self.dispatches.get(inst.pc) is Some(dispatch) {
        for target in dispatch.targets {
          for callee in callees(target) { add_entry(callee, 2, 0) }
        }
      } else {
        for target in callees(jump_target(inst)) { add_entry(target, 2, 0) }
      }
    }
    // (a computed jump at an interrupt's entry continues the interrupt:
    // its targets are checked as such below)
    if self.jumps.get(inst.pc) is Some(targets) && !self.roots[1:].iter().any(fn(r) { self.image.symbols.get(r) == Some(inst.pc) }) {
      for target in targets {
        for callee in callees(target) { add_entry(callee, 2, 0) }
      }
    }
  }
  // Routines a jump with the token dropped leaves to, as a call.
  let routines: Set[Int] = Set(entries.map(fn(e) { e.0 }))
  let leaks: Array[String] = []
  let reset_txs: Set[Int] = Set([])
  let mut index = 0
  while index < entries.length() {
    let (entry, kind, start) = entries[index]
    index += 1
    let is_reset = kind == 0
    let is_interrupt = kind == 1
    let states: Map[(Int, Int), Facts] = Map([])
    let work = [(entry, start, self.facts[entry])]
    while !work.is_empty() {
      let (pc, depth, incoming) = work.pop().unwrap()
      let state = match states.get((pc, depth)) {
        Some(previous) => {
          let joined = previous.join(incoming)
          if previous == joined { continue }
          joined
        }
        None => incoming
      }
      states[(pc, depth)] = state
      if self.image.is_data(pc) { continue } // a trap
      let inst = self.instructions.get(pc).unwrap_or_error(InvalidImage("Missing instruction in stack analysis: \{pc}"))
      let after = transfer(inst, state)
      let mut next_depth = depth
      match inst.op {
        PHA | PHP => next_depth += 1
        PLA | PLP => next_depth -= 1
        TXS => {
          if is_interrupt { raise InvalidImage("Stack-pointer replacement in an interrupt handler at \{show_pc(pc)}") }
          if !is_reset {
            // a restart: the reset routine continues from here, with
            // nothing pending (checked below)
            if !self.restarts.contains(pc) { self.restarts.push(pc) }
            continue
          }
          reset_txs.add(pc)
          next_depth = 0
        }
        _ => ()
      }
      if next_depth > 253 {
        // with the machine's stack the map asks for, whatever the depth (a
        // loop pushing as its data says): returns go where the bytes pulled
        // say
        if !self.map.exact { raise InvalidImage("Excessive stack depth at \{show_pc(pc)} from \{address_name(self.image, entry)}") }
        next_depth = 253
      }
      if next_depth < 0 {
        // the routine reaches its return address
        if is_reset || is_interrupt { raise InvalidImage("Return-token access in an interrupt or reset entry at \{show_pc(pc)}") }
        self.exact.val = true
        if next_depth < -16 { next_depth = -16 }
      }
      if inst.op == RTI && !is_interrupt { raise InvalidImage("Interrupt return outside an interrupt entry at \{show_pc(pc)}") }
      if inst.op == RTS && is_interrupt { raise InvalidImage("Subroutine return from an interrupt entry at \{show_pc(pc)}") }
      if inst.op == RTS && depth == 4 && self.computed_call(pc) {
        // a computed call: back after it, the four bytes pulled
        work.push((inst.next, 0, Facts::unknown()))
        continue
      }
      if inst.op == RTS && depth == 2 && self.jumps.contains(pc) { continue } // a computed jump
      if inst.op == RTS || inst.op == RTI {
        if depth > 0 && inst.op == RTS {
          // with exact returns, it returns where the bytes pulled say, as
          // the machine does (a path games take only by mistake)
          if leaks.is_empty() {
            leaks.push("RTS at \{show_pc(pc)} returns through \{depth} byte(s) pushed by the routine (computed jump); only proven inline dispatchers are supported")
          }
          continue
        }
        // a return through part of a return address goes nowhere known
        // (with the machine's stack the map asks for, wherever the bytes
        // pulled say)
        if (depth > 0 || depth % 2 != 0 || (inst.op == RTI && depth != 0)) && !(self.map.exact && inst.op == RTS) {
          raise InvalidImage("Unbalanced return at \{show_pc(pc)}")
        }
        continue
      }
      if self.idle.contains(pc) {
        if !is_reset { raise InvalidImage("An idle loop outside reset requires a resumable continuation at \{show_pc(pc)}") }
        continue
      }
      if self.dispatches.get(pc) is Some(dispatch) {
        if depth != 0 { raise InvalidImage("Inline dispatch with pending data stack at \{show_pc(pc)}") }
        // The helper consumes only its own return token; selected handlers
        // return on this entry's incoming token and are checked independently.
        for target in dispatch.targets {
          // (one in another window is the routine of the bank mapped there)
          if self.windows.contains(target) {
            for callee in callees(target) { add_entry(callee, 2, 0) }
          } else {
            work.push((target, depth, self.facts[target]))
          }
        }
      } else if inst.mode == "rel" {
        let condition = condition(inst.op, state)
        if condition != Some(false) { work.push((branch_target(inst), next_depth, assume_condition(inst.op, true, after))) }
        if condition != Some(true) { work.push((inst.next, next_depth, assume_condition(inst.op, false, after))) }
      } else if inst.op == JMP {
        // Jumps into the window or through RAM leave as tail calls, which
        // the targets' own entries check, so they leave nothing pending; so
        // does a jump into another routine once the token is dropped.
        let tail = if inst.mode == "abs" {
          if self.windows.contains(jump_target(inst)) || (depth < 0 && routines.contains(jump_target(inst))) {
            Some(callees(jump_target(inst)))
          } else { None }
        } else { self.jumps.get(pc) }
        let window = inst.mode == "abs" && self.windows.contains(jump_target(inst))
        match tail {
          // the routines of the window continue this entry, with its data
          Some(targets) if window && next_depth > 0 =>
            for target in targets { add_entry(target, kind, next_depth) }
          Some(targets) if window => for target in targets { add_entry(target, 2, 0) }
          Some(targets) => {
            if next_depth > 0 { raise InvalidImage("Tail jump with pending data stack at \{show_pc(pc)}") }
            // a computed jump continues this entry (an interrupt's, say)
            if !window && inst.mode != "abs" {
              for target in targets {
                for callee in callees(target) { add_entry(callee, kind, 0) }
              }
            }
          }
          None =>
            if inst.mode == "abs" { work.push((jump_target(inst), next_depth, after)) }
            else {
              guard rom_vector(self.image, inst) is Some(target) else { raise InvalidImage("Unresolved indirect transfer at \{show_pc(pc)}") }
              work.push((target, next_depth, after))
            }
        }
      } else if inst.op == JSR && always_unwinds(self.image, jump_target(inst)) {
        // the callee returns from this routine, through its return address
        self.exact.val = true
      } else if inst.op == JSR {
        work.push((self.image.continuation(pc, inst.next, jump_target(inst)), next_depth, after))
      } else { work.push((inst.next, next_depth, after)) }
    }
  }
  if !self.exact.val && leaks.length() > 0 { raise InvalidImage(leaks[0]) }
  // a restart continues the reset routine: its code must be the reset's
  for pc in self.restarts {
    if !reset_txs.contains(pc) { raise InvalidImage("Stack-pointer replacement outside reset at \{show_pc(pc)}") }
  }
  self.restarts.sort()
}
