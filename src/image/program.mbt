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
  { pc, op, mode, arg, next: (pc + size) & 65535 }
}

pub(all) struct Program {
  image: Image
  instructions: Map[Int, Instruction]
  dispatches: Map[Int, Dispatch]
  roots: Array[String]
  facts: Map[Int, Facts]
  idle: Set[Int]
  source: Array[@parse.SourceItem]
  addresses: Array[Int]
} derive(Debug)

pub fn address_name(image: Image, pc: Int) -> String {
  // Preserve source order for aliases, independent of hash-map iteration.
  for i, item in image.source {
    if image.addresses[i] == pc && item is Label(name) { return name }
  }
  "loc_\{pc}"
}

pub fn branch_target(inst: Instruction) -> Int {
  (inst.next + (if inst.arg >= 128 { inst.arg - 256 } else { inst.arg })) & 65535
}

pub fn source_mode(image: Image, inst: Instruction) -> @parse.SourceAddressingMode {
  let e = @parse.SourceExpr::Const(base=Hexadecimal, value=inst.arg, size=if width(inst.mode) == 2 { Byte } else { Word })
  match inst.mode {
    "imp" => Implied
    "acc" => Accumulator
    "imm" => Immediate(e)
    "zp" | "abs" => {
      if inst.op == JMP || inst.op == JSR {
        AbsoluteOrZeroPage(addr=Ident(address_name(image, inst.arg)))
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

pub fn discover(image: Image) -> Program raise ImageError {
  if !((image.bytes.length() == 32768 && image.origin == 0x8000) ||
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
  let facts: Map[Int, Facts] = Map([])
  let work = roots.map(fn(pc) { (pc, Facts::unknown()) })
  while !work.is_empty() {
    let (address, incoming) = work.pop().unwrap()
    let pc = address
    if pc < image.origin || pc >= image.origin + image.bytes.length() {
      raise InvalidImage("Execution outside the source address window at \{show_pc(pc)}; mirrored code needs an explicit source view")
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
    let inst = decode(image, pc)
    instructions[pc] = inst
    if inst.op == CLI || inst.op == PLP || inst.op == BRK {
      let irq = image.word(canonical(0xfffe))
      if !roots.contains(irq) { roots.push(irq); work.push((irq, Facts::unknown())) }
    }
    let after = transfer(inst, f)
    match (inst.op, inst.mode) {
      (JSR, _) => {
        let table = data_at.get(inst.next).map(fn(i) { inline_data_end(image, i) })
        match table.map(fn(end) { dispatch_at(image, inst, end) }) {
          Some(Some(dispatch)) => {
            dispatches[pc] = dispatch
            for target in dispatch.targets { work.push((target, Facts::unknown())) }
          }
          // Returning into data is never decoded as a guess.
          Some(None) => raise InvalidImage("Call at \{show_pc(pc)} returns into inline data at \{show_pc(inst.next)}, but \{address_name(image, inst.arg)} was not proven to be an inline dispatcher")
          None => {
            work.push((inst.arg, f)); work.push((inst.next, after))
          }
        }
      }
      (JMP, "abs") => {
        if inst.arg == pc { idle.add(pc) } else { work.push((inst.arg, after)) }
      }
      (JMP, "ind") => {
        // Immutable ROM vectors are resolvable without a dispatcher summary.
        let hi = (inst.arg & 0xff00) | ((inst.arg + 1) & 255)
        if inst.arg >= image.origin && hi >= image.origin {
          work.push((image.read(inst.arg) | (image.read(hi) << 8), after))
        } else { raise InvalidImage("Unresolved indirect transfer at \{show_pc(pc)}") }
      }
      (RTS | RTI, _) => ()
      (BRK, _) => raise InvalidImage("BRK requires interrupt scheduling at \{show_pc(pc)}")
      (_, "rel") => {
        if condition(inst.op, f) != Some(false) { work.push((branch_target(inst), assume_condition(inst.op, true, after))) }
        if condition(inst.op, f) != Some(true) { work.push((inst.next, assume_condition(inst.op, false, after))) }
      }
      _ => work.push((inst.next, after))
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
    if inst.op == JMP || inst.op == JSR {
      if inst.mode == "ind" {
        let hi = (inst.arg & 0xff00) | ((inst.arg + 1) & 255)
        needed.add(image.read(inst.arg) | (image.read(hi) << 8))
      } else { needed.add(inst.arg) }
    }
    if following.get(pc) != Some(inst.next) { needed.add(inst.next) }
  }
  for dispatch in dispatches.values() {
    for target in dispatch.targets { needed.add(target) }
  }
  check_wait_loops(image, instructions, facts, idle)
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
        } else if inst.op == JMP && inst.mode == "ind" {
          let hi = (inst.arg & 0xff00) | ((inst.arg + 1) & 255)
          let target = image.read(inst.arg) | (image.read(hi) << 8)
          append(Inst(op=JMP, mode=AbsoluteOrZeroPage(addr=Ident(address_name(image, target)))), pc)
        } else if branch != Some(false) {
          append(original, pc)
        }
        // Address overlap can put a different instruction stream next in
        // source order. Preserve the actual fallthrough explicitly.
        let falls = inst.op != JMP && inst.op != RTS && inst.op != RTI &&
          inst.op != BRK && !dispatches.contains(pc) && branch != Some(true)
        if falls && following.get(pc) != Some(inst.next) {
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
  { image, instructions, dispatches, roots: roots.map(fn(pc) { address_name(image, pc) }), facts, idle, source, addresses }
}
