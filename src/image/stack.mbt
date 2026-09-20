// Check the contract used by the function backend. Depth counts data above a
// normal return token. A call is balanced only after its callee has also been
// checked. Nonstandard return manipulation is diagnosed, not silently mapped
// to a C return. The inline-dispatch proof is its own explicit exception.
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
  for inst in self.instructions.values() {
    if inst.op == JSR {
      if self.dispatches.get(inst.pc) is Some(dispatch) {
        for target in dispatch.targets { add_entry(target, 2) }
      } else { add_entry(inst.arg, 2) }
    }
  }
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
      let inst = self.instructions.get(pc).unwrap_or_error(InvalidImage("Missing instruction in stack analysis: \{pc}"))
      let after = transfer(inst, state)
      let mut next_depth = depth
      match inst.op {
        PHA | PHP => next_depth += 1
        PLA | PLP => next_depth -= 1
        TXS => {
          if !is_reset { raise InvalidImage("Stack-pointer replacement outside reset at \{pc}") }
          next_depth = 0
        }
        _ => ()
      }
      if next_depth < 0 || next_depth > 253 { raise InvalidImage("Return-token access or excessive stack depth at \{pc} from \{address_name(self.image, entry)}") }
      if inst.op == RTI && !is_interrupt { raise InvalidImage("Interrupt return outside an interrupt entry at \{pc}") }
      if inst.op == RTS && is_interrupt { raise InvalidImage("Subroutine return from an interrupt entry at \{pc}") }
      if inst.op == RTS || inst.op == RTI {
        if depth != 0 { raise InvalidImage("Unbalanced return at \{pc}") }
        continue
      }
      if self.idle.contains(pc) {
        if !is_reset { raise InvalidImage("An idle loop outside reset requires a resumable continuation at \{pc}") }
        continue
      }
      if self.dispatches.get(pc) is Some(dispatch) {
        if depth != 0 { raise InvalidImage("Inline dispatch with pending data stack at \{pc}") }
        // The helper consumes only its own return token; selected handlers
        // return on this entry's incoming token and are checked independently.
        for target in dispatch.targets { work.push((target, depth, self.facts[target])) }
      } else if inst.mode == "rel" {
        let condition = condition(inst.op, state)
        if condition != Some(false) { work.push((branch_target(inst), next_depth, assume_condition(inst.op, true, after))) }
        if condition != Some(true) { work.push((inst.next, next_depth, assume_condition(inst.op, false, after))) }
      } else if inst.op == JMP {
        if inst.mode == "abs" { work.push((inst.arg, next_depth, after)) }
        else {
          let hi = (inst.arg & 0xff00) | ((inst.arg + 1) & 255)
          work.push((self.image.read(inst.arg) | (self.image.read(hi) << 8), next_depth, after))
        }
      } else { work.push((inst.next, next_depth, after)) }
    }
  }
}
