pub(all) struct Reachability {
  executable_bytes: Set[Int]
  unreachable_bytes: Set[Int]
} derive(Debug)

// A byte can be both executable and source-level data: overlapping 6502
// encodings make that normal.  The two sets describe execution reachability,
// not a lossy partition of the ROM image.
pub fn classify_reachability(image: @layout.Image, cfg: ControlFlowGraph) -> Reachability {
  let executable_bytes = Set([])
  for instruction in cfg.instructions.values() {
    for offset in 0..<instruction.size {
      executable_bytes.add(instruction.address + offset)
    }
  }

  let unreachable_bytes = Set([])
  for offset in 0..<image.bytes.length() {
    let address = image.origin + offset
    if !executable_bytes.contains(address) {
      unreachable_bytes.add(address)
    }
  }

  { executable_bytes, unreachable_bytes }
}

pub fn Reachability::is_executable(self: Reachability, address: Int) -> Bool {
  self.executable_bytes.contains(address)
}

pub fn Reachability::is_unreachable(self: Reachability, address: Int) -> Bool {
  self.unreachable_bytes.contains(address)
}
