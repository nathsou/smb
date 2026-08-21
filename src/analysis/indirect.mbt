pub(all) struct DispatchSite {
  call_address: Int
  dispatcher: Int
  table_address: Int
  targets: Array[Int]
} derive(Debug)

fn indirect_jump_in(view: RoutineView) -> Bool {
  view.instructions.values().iter().any(fn(instruction) {
    match instruction.flow {
      IndirectJump(_) => true
      _ => false
    }
  })
}

fn inline_targets(image: @layout.Image, start: Int) -> Array[Int] {
  let targets = []
  let mut address = start
  while true {
    match image.word_targets.get(address) {
      Some(target) => {
        targets.push(target)
        address += 2
      }
      None => break
    }
  }
  targets
}

// Recognizes return-address indexed dispatch structurally: the callee consumes
// its own return token, reaches an indirect JMP, and the bytes after the call
// are relocatable PRG words. No source symbol such as `JumpEngine` is used.
pub fn resolve_inline_dispatches(image: @layout.Image, cfg: ControlFlowGraph) -> Array[DispatchSite] {
  let dispatches = []
  for instruction in cfg.instructions.values() {
    match instruction.flow {
      Call(target=dispatcher, fallthrough=table_address) => {
        let view = recover_routine_view(cfg, dispatcher)
        let stack = summarize_stack(view)
        let targets = inline_targets(image, table_address)
        if stack.pops_return_address && indirect_jump_in(view) && !targets.is_empty() {
          dispatches.push({ call_address: instruction.address, dispatcher, table_address, targets })
        }
      }
      _ => ()
    }
  }
  dispatches.sort_by(fn(lhs, rhs) { lhs.call_address - rhs.call_address })
  dispatches
}
