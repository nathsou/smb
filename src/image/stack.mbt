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
  let entries: Array[(Int, Int)] = []
  fn add_entry(entry: Int, kind: Int) {
    if !entries.contains((entry, kind)) { entries.push((entry, kind)) }
  }
  for i, root in self.roots {
    add_entry(self.image.symbols[root], if i == 0 { 0 } else { 1 })
  }
  // A call into the window from the fixed bank calls whichever routine is
  // mapped there; a computed jump continues in its target.
  fn callees(target: Int) -> Array[Int] {
    match self.windows.get(target) { Some(banked) => banked; None => [target] }
  }
  for inst in self.instructions.values() {
    if inst.op == JSR {
      if self.dispatches.get(inst.pc) is Some(dispatch) {
        for target in dispatch.targets { add_entry(target, 2) }
      } else {
        for target in callees(jump_target(inst)) { add_entry(target, 2) }
      }
    }
    if self.jumps.get(inst.pc) is Some(targets) {
      for target in targets {
        for callee in callees(target) { add_entry(callee, 2) }
      }
    }
    if inst.op == JMP && inst.mode == "abs" && self.windows.contains(jump_target(inst)) {
      for target in callees(jump_target(inst)) { add_entry(target, 2) }
    }
  }
  // Routines a jump with the token dropped leaves to, as a call.
  let routines: Set[Int] = Set(entries.map(fn(e) { e.0 }))
  for entry_and_kind in entries {
    let (entry, kind) = entry_and_kind
    let is_reset = kind == 0
    let is_interrupt = kind == 1
    let states: Map[(Int, Int), Facts] = Map([])
    let work = [(entry, 0, self.facts[entry])]
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
          if !is_reset { raise InvalidImage("Stack-pointer replacement outside reset at \{show_pc(pc)}") }
          next_depth = 0
        }
        _ => ()
      }
      if next_depth > 253 { raise InvalidImage("Excessive stack depth at \{show_pc(pc)} from \{address_name(self.image, entry)}") }
      if next_depth < 0 {
        // the routine reaches its return address
        if is_reset || is_interrupt { raise InvalidImage("Return-token access in an interrupt or reset entry at \{show_pc(pc)}") }
        self.exact.val = true
        if next_depth < -16 { next_depth = -16 }
      }
      if inst.op == RTI && !is_interrupt { raise InvalidImage("Interrupt return outside an interrupt entry at \{show_pc(pc)}") }
      if inst.op == RTS && is_interrupt { raise InvalidImage("Subroutine return from an interrupt entry at \{show_pc(pc)}") }
      if inst.op == RTS && depth == 2 && self.jumps.contains(pc) { continue } // a computed jump
      if inst.op == RTS || inst.op == RTI {
        if depth > 0 && inst.op == RTS {
          raise InvalidImage("RTS at \{show_pc(pc)} returns through \{depth} byte(s) pushed by the routine (computed jump); only proven inline dispatchers are supported")
        }
        // a return through part of a return address goes nowhere known
        if depth > 0 || depth % 2 != 0 || (inst.op == RTI && depth != 0) { raise InvalidImage("Unbalanced return at \{show_pc(pc)}") }
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
        for target in dispatch.targets { work.push((target, depth, self.facts[target])) }
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
        match tail {
          Some(_) => if next_depth > 0 { raise InvalidImage("Tail jump with pending data stack at \{show_pc(pc)}") }
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
      } else { work.push((inst.next, next_depth, after)) }
    }
  }
}
