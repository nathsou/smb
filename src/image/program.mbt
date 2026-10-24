pub(all) struct Instruction {
  pc: Int
  op: @parse.OpCode
  mode: String
  arg: Int
  next: Int
} derive(Debug)

pub fn decode(image: Image, pc: Int) -> Instruction raise ImageError {
  let (op, mode) = decoding(image.read(pc)).unwrap_or_error(InvalidImage("Unsupported opcode at \{show_pc(pc)}"))
  let size = width(mode)
  let arg = if size == 1 { 0 } else if size == 2 { image.read(pc + 1) } else { image.word(pc + 1) }
  { pc, op, mode, arg, next: rebase(pc, (pc & 0xffff) + size) }
}

pub(all) struct Program {
  image: Image
  instructions: Map[Int, Instruction]
  dispatches: Map[Int, Dispatch]
  roots: Array[String]
  facts: Map[Int, Facts]
  idle: Set[Int]
  // Window addresses called or jumped to from outside their window (fixed
  // banks, or another window), with the locations of the routines that can
  // be mapped there.
  windows: Map[Int, Array[Int]]
  // Computed jumps through RAM, with the locations they can reach.
  jumps: Map[Int, Array[Int]]
  // Wait loops: the transfer back to each loop's start, which waits for the
  // next interrupt (see loops.mbt), and that start.
  waits: Map[Int, Int]
  // Whether the program reads return addresses, so that calls must keep
  // them on the guest stack (see stack.mbt). Set by the stack analysis.
  exact: Ref[Bool]
  source: Array[@parse.SourceItem]
  addresses: Array[Int]
  map: RomMap // for a ROM; empty for assembly
} derive(Debug)

// Transfers into data the map names go to a trap at that address.
pub fn trap_name(pc: Int) -> String {
  "__trap__" + pc.to_string(radix=16)
}

pub fn trap_address(name: String) -> Int? {
  guard name.has_prefix("__trap__") else { return None }
  try Some(@string.parse_int(name.view(start_offset=8).to_owned(), base=16)) catch { _ => None }
}

pub fn address_name(image: Image, pc: Int) -> String {
  // Preserve source order for aliases, independent of hash-map iteration.
  for i, item in image.source {
    if image.addresses[i] == pc && item is Label(name) { return name }
  }
  if image.banks.window_names.get(pc) is Some(name) { return name }
  match bank_of(pc) {
    Some(bank) => "loc_\{image.banks.names[bank]}_\{(pc & 0xffff).to_string(radix=16)}"
    None => "loc_\{pc.to_string(radix=16)}"
  }
}

pub fn branch_target(inst: Instruction) -> Int {
  rebase(inst.pc, (inst.next & 0xffff) + (if inst.arg >= 128 { inst.arg - 256 } else { inst.arg }))
}

// The location a JSR or JMP leads to.
pub fn jump_target(inst: Instruction) -> Int {
  rebase(inst.pc, inst.arg)
}

pub fn source_mode(image: Image, inst: Instruction) -> @parse.SourceAddressingMode {
  let e = @parse.SourceExpr::Const(base=Hexadecimal, value=inst.arg, size=if width(inst.mode) == 2 { Byte } else { Word })
  match inst.mode {
    "imp" => Implied
    "acc" => Accumulator
    "imm" => Immediate(e)
    "zp" | "abs" => {
      if inst.op == JMP || inst.op == JSR {
        AbsoluteOrZeroPage(addr=Ident(address_name(image, jump_target(inst))))
      } else { AbsoluteOrZeroPage(addr=e) }
    }
    "zpx" | "absx" => AbsoluteOrZeroPageX(addr=e)
    "zpy" | "absy" => AbsoluteOrZeroPageY(addr=e)
    "indx" => IndexedIndirect(addr=e)
    "indy" => IndirectIndexed(addr=e)
    "ind" => Indirect(addr=e)
    "rel" => Relative(addr=Ident(address_name(image, branch_target(inst))))
    _ => abort("Invalid decoded addressing mode")
  }
}

// Decoded control flow, independent of how the program was supplied.
priv struct Exploration {
  instructions: Map[Int, Instruction]
  dispatches: Map[Int, Dispatch]
  roots: Array[Int]
  facts: Map[Int, Facts]
  idle: Set[Int]
  windows: Map[Int, Array[Int]]
  jumps: Map[Int, Array[Int]]
  waits: Map[Int, Int]
  traps: Set[Int] // data that some transfer reaches
  needed: Set[Int] // addresses that need a label (jump and call targets, entries)
  following: Map[Int, Int] // next decoded instruction address
  pcs: Array[Int] // decoded instruction addresses, ascending
}

// The target of `jmp (pointer)` when the pointer is in ROM, which never
// changes.
fn rom_vector(image: Image, inst: Instruction) -> Int? raise ImageError {
  let hi = (inst.arg & 0xff00) | ((inst.arg + 1) & 255)
  if image.banked() {
    let pointer = rebase(inst.pc, inst.arg)
    let high = rebase(inst.pc, hi)
    // a window as mapped at run time is not known
    if inst.arg < 0x8000 || hi < 0x8000 || (in_window(pointer) && bank_of(pointer) is None) ||
      (in_window(high) && bank_of(high) is None) { return None }
    return Some(rebase(pointer, image.read(pointer) | (image.read(high) << 8)))
  }
  if inst.arg >= image.origin && hi >= image.origin {
    Some(image.read(inst.arg) | (image.read(hi) << 8))
  } else { None }
}

// A routine that runs straight into `pla; pla; rts` without touching the
// stack otherwise never returns to its caller: it drops the return address,
// and returns to the caller's caller.
fn always_unwinds(image: Image, entry: Int) -> Bool {
  let mut pc = entry
  let mut pulls = 0
  for _ in 0..<32 {
    let inst = decode(image, pc) catch { _ => return false }
    match inst.op {
      PLA if pulls < 2 => pulls += 1
      RTS => return pulls == 2
      JMP if inst.mode == "abs" => {
        pc = jump_target(inst)
        continue
      }
      PLA | PHA | PHP | PLP | TXS | TSX | JSR | JMP | RTI | BRK => return false
      _ => if inst.mode == "rel" || pulls == 1 { return false }
    }
    pc = inst.next
  }
  false
}

// Where a computed jump at `site` (through RAM, or returning through an
// address the routine pushed) can go: the addresses in the map's code tables
// of its bank (the fixed bank's for code in the fixed bank).
fn code_targets(image: Image, site: Int) -> Array[Int] raise ImageError {
  let targets = []
  for code in image.banks.code {
    let (location, adjust) = code
    if bank_of(location) != bank_of(site) { continue }
    let target = rebase(location, image.word(location) + adjust)
    if !targets.contains(target) { targets.push(target) }
  }
  targets
}

// Follows execution from the reset and NMI vectors. Inline tables after a
// call are the data directives in `image.source` that start at its return
// address (from the assembly source, or from a ROM map).
fn explore(image: Image) -> Exploration raise ImageError {
  if image.banked() {
    if image.origin + image.bytes.length() != 0x10000 || fixed_bank(image.origin) < 0 {
      raise InvalidImage("A banked image needs its fixed banks up to $FFFF")
    }
  } else if !((image.bytes.length() == 32768 && image.origin == 0x8000) ||
       (image.bytes.length() == 16384 && (image.origin == 0x8000 || image.origin == 0xc000))) {
    raise InvalidImage("NROM requires a 16 or 32 KiB PRG image in a cartridge window")
  }
  // First source item of each data directive, for inline-table extents.
  let data_at: Map[Int, Int] = Map([])
  for i, item in image.source {
    if item is Directive(name="db" | "dw", ..) && !data_at.contains(image.addresses[i]) {
      data_at[image.addresses[i]] = i
    }
  }
  let roots = []
  // NROM-128 is mirrored at $C000. The normalized image may be placed in
  // either window; canonicalize vector addresses to its physical byte range.
  fn canonical(pc: Int) -> Int {
    if image.bytes.length() == 16384 && pc >= 0x8000 {
      image.origin + ((pc - 0x8000) & 0x3fff)
    } else { pc }
  }
  for vector in [0xfffc, 0xfffa] {
    let address = canonical(vector)
    if address >= image.origin && address + 1 < image.origin + image.bytes.length() {
      let target = image.word(address)
      // Reset sets I. IRQ is discovered below if reachable code can clear it.
      if !roots.contains(target) { roots.push(target) }
    }
  }
  if roots.is_empty() { raise InvalidImage("No reset/interrupt vectors in image") }
  let instructions: Map[Int, Instruction] = Map([])
  let dispatches: Map[Int, Dispatch] = Map([])
  let idle: Set[Int] = Set([])
  let windows: Map[Int, Array[Int]] = Map([])
  let jumps: Map[Int, Array[Int]] = Map([])
  let traps: Set[Int] = Set([])
  let facts: Map[Int, Facts] = Map([])
  // the first transfer to each location, to explain how bad code was reached
  let parents: Map[Int, Int] = Map([])
  fn path(pc: Int) -> String {
    let steps = []
    let mut at = pc
    while steps.length() < 12 && parents.get(at) is Some(p) && p >= 0 {
      steps.push(show_pc(p))
      at = p
    }
    steps.join(" <- ")
  }
  let work = roots.map(fn(pc) { (pc, Facts::unknown().entry(), -1) })
  while !work.is_empty() {
    let (address, incoming, from) = work.pop().unwrap()
    let pc = address
    if !parents.contains(pc) { parents[pc] = from }
    if image.banked() && pc < 0x10000 && in_window(pc) {
      // Code outside a window reaches it: whichever bank is mapped, among
      // the routines the map names at this address.
      if windows.contains(pc) { continue }
      guard image.banks.windows.get(pc) is Some(banks) else {
        raise InvalidImage("Code outside the window reaches \{show_pc(pc)} in a switchable window, from \{path(pc)}, but the map names no routine there in any bank (routine BANK:\{show_pc(pc)} NAME)")
      }
      windows[pc] = banks.map(fn(bank) { bank_location(bank, pc) })
      for target in windows[pc] { work.push((target, Facts::unknown().entry(), pc)) }
      continue
    }
    let inside = if image.banked() {
      (bank_of(pc) is Some(bank) && bank < image.banks.contents.length()) || fixed_bank(pc) >= 0
    } else { pc >= image.origin && pc < image.origin + image.bytes.length() }
    if !inside {
      raise InvalidImage("Execution outside the source address window at \{show_pc(pc)}, reached from \{path(pc)}; mirrored code needs an explicit source view")
    }
    if image.is_data(pc) {
      // A path the program never takes when its tables hold what they
      // should: it traps rather than run data.
      traps.add(pc)
      continue
    }
    let f = match facts.get(pc) {
      Some(previous) => {
        let joined = previous.join(incoming)
        if joined == previous { continue }
        joined
      }
      None => incoming
    }
    facts[pc] = f
    let inst = decode(image, pc) catch {
      InvalidImage(message) => raise InvalidImage("\{message}, reached from \{path(pc)}")
    }
    instructions[pc] = inst
    if inst.op == CLI || inst.op == PLP || inst.op == BRK {
      let irq = image.word(canonical(0xfffe))
      if !roots.contains(irq) { roots.push(irq); work.push((irq, Facts::unknown().entry(), pc)) }
    }
    let after = transfer(inst, f)
    match (inst.op, inst.mode) {
      (JSR, _) => {
        let table = data_at.get(inst.next).map(fn(i) { inline_data_end(image, i) })
        match table.map(fn(end) { dispatch_at(image, inst, end) }) {
          Some(Some(dispatch)) => {
            dispatches[pc] = dispatch
            for target in dispatch.targets { work.push((target, Facts::unknown().entry(), pc)) }
          }
          // a callee that returns past this routine: whatever follows the
          // call is not reached from it
          _ if always_unwinds(image, jump_target(inst)) => work.push((jump_target(inst), f.entry(), pc))
          // Returning into data is never decoded as a guess.
          Some(None) => raise InvalidImage("Call at \{show_pc(pc)} returns into inline data at \{show_pc(inst.next)}, but \{address_name(image, jump_target(inst))} was not proven to be an inline dispatcher")
          None => {
            work.push((jump_target(inst), f.entry(), pc)); work.push((inst.next, after, pc))
          }
        }
      }
      (JMP, "abs") => {
        if jump_target(inst) == pc { idle.add(pc) } else { work.push((jump_target(inst), after, pc)) }
      }
      (JMP, "ind") => {
        // Immutable ROM vectors are resolvable without a dispatcher summary;
        // pointers in RAM can reach the map's code addresses.
        match rom_vector(image, inst) {
          Some(target) => work.push((target, after, pc))
          None => {
            let targets = code_targets(image, pc)
            if targets.is_empty() { raise InvalidImage("Unresolved indirect transfer at \{show_pc(pc)}") }
            jumps[pc] = targets
            for target in targets { work.push((target, Facts::unknown().entry(), pc)) }
          }
        }
      }
      (RTS, _) if f.depth == Some(2) => {
        // returning through an address the routine pushed: a computed jump
        let targets = code_targets(image, pc)
        if !targets.is_empty() {
          jumps[pc] = targets
          for target in targets { work.push((target, Facts::unknown().entry(), pc)) }
        }
      }
      (RTS | RTI, _) => ()
      (BRK, _) => raise InvalidImage("BRK requires interrupt scheduling at \{show_pc(pc)}")
      (_, "rel") => {
        if condition(inst.op, f) != Some(false) { work.push((branch_target(inst), assume_condition(inst.op, true, after), pc)) }
        if condition(inst.op, f) != Some(true) { work.push((inst.next, assume_condition(inst.op, false, after), pc)) }
      }
      _ => work.push((inst.next, after, pc))
    }
  }
  let pcs = instructions.keys().to_array()
  pcs.sort()
  let following: Map[Int, Int] = Map([])
  let needed: Set[Int] = Set(roots)
  for i, pc in pcs {
    if i + 1 < pcs.length() { following[pc] = pcs[i + 1] }
    let inst = instructions[pc]
    if inst.mode == "rel" { needed.add(branch_target(inst)) }
    if inst.op == RTS && jumps.get(pc) is Some(targets) {
      for target in targets { needed.add(target) }
    }
    if inst.op == JMP || inst.op == JSR {
      if inst.mode == "ind" {
        match rom_vector(image, inst) {
          Some(target) => needed.add(target)
          None => for target in jumps[pc] { needed.add(target) }
        }
      } else { needed.add(jump_target(inst)) }
    }
    if following.get(pc) != Some(inst.next) { needed.add(inst.next) }
  }
  for dispatch in dispatches.values() {
    for target in dispatch.targets { needed.add(target) }
  }
  for targets in windows.values() {
    for target in targets { needed.add(target) }
  }
  let waits = find_waits(instructions, facts, idle)
  check_wait_loops(image, instructions, facts, idle, waits)
  { instructions, dispatches, roots, facts, idle, windows, jumps, waits, traps, needed, following, pcs }
}

pub fn discover(image: Image) -> Program raise ImageError {
  let { instructions, dispatches, roots, facts, idle, windows, jumps, waits, needed, following, pcs, .. } = explore(image)
  // Assign decoded addresses to byte-producing source spans, allowing several
  // executable entries inside one .db or one source instruction's encoding.
  let owners: Map[Int, Int] = Map([])
  let original_instructions: Set[Int] = Set([])
  for i, item in image.source {
    let pc = image.addresses[i]
    let size = match item {
      Inst(_) => { original_instructions.add(pc); decode(image, pc).next - pc }
      Directive(name="db", args~) => args.length()
      Directive(name="dw", args~) => args.length() * 2
      _ => 0
    }
    for j in 0..<size { owners[pc + j] = i }
  }
  let spans: Map[Int, Array[Int]] = Map([])
  for pc in pcs {
    let owner = owners.get(pc).unwrap_or_error(InvalidImage("Execution in an origin gap at \{show_pc(pc)}"))
    if !spans.contains(owner) { spans[owner] = [] }
    spans[owner].push(pc)
  }
  let source = []
  let addresses = []
  fn append(item: @parse.SourceItem, pc: Int) {
    source.push(item)
    addresses.push(pc)
  }
  for pc in needed {
    if instructions.contains(pc) {
      let name = address_name(image, pc)
      if !image.symbols.contains(name) { image.symbols[name] = pc }
    }
  }
  for i, item in image.source {
    let source_pc = image.addresses[i]
    match spans.get(i) {
      Some(decoded) => for pc in decoded {
        let inst = instructions[pc]
        let name = address_name(image, pc)
        if name.has_prefix("loc_") && needed.contains(pc) { append(Label(name), pc) }
        let original = if source_pc == pc && item is Inst(_) && inst.op != JMP && inst.op != JSR && inst.mode != "rel" { item }
          else { @parse.SourceItem::Inst(op=inst.op, mode=source_mode(image, inst)) }
        let branch = if inst.mode == "rel" { condition(inst.op, facts[pc]) } else { None }
        if branch == Some(true) {
          append(Inst(op=JMP, mode=AbsoluteOrZeroPage(addr=Ident(address_name(image, branch_target(inst))))), pc)
        } else if inst.op == JMP && inst.mode == "ind" && rom_vector(image, inst) is Some(target) {
          append(Inst(op=JMP, mode=AbsoluteOrZeroPage(addr=Ident(address_name(image, target)))), pc)
        } else if branch != Some(false) {
          append(original, pc)
        }
        // Address overlap can put a different instruction stream next in
        // source order. Preserve the actual fallthrough explicitly.
        let falls = inst.op != JMP && inst.op != RTS && inst.op != RTI &&
          inst.op != BRK && !dispatches.contains(pc) && branch != Some(true)
        if inst.op == JSR && always_unwinds(image, jump_target(inst)) {
          // the callee returns past this routine: nothing follows the call
          append(Inst(op=JMP, mode=AbsoluteOrZeroPage(addr=Ident(trap_name(inst.next)))), pc)
        } else if falls && following.get(pc) != Some(inst.next) {
          append(Inst(op=JMP, mode=AbsoluteOrZeroPage(addr=Ident(address_name(image, inst.next)))), pc)
        }
      }
      None => match item {
        Inst(_) => ()
        Label(_) if original_instructions.contains(source_pc) && !instructions.contains(source_pc) => ()
        _ => append(item, source_pc)
      }
    }
  }
  { image, instructions, dispatches, roots: roots.map(fn(pc) { address_name(image, pc) }), facts, idle, windows, jumps, waits, exact: Ref(false), source, addresses, map: RomMap::empty() }
}
