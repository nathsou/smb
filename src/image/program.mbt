pub(all) struct Instruction {
  pc: Int
  op: @parse.OpCode
  mode: String
  arg: Int
  next: Int
} derive(Debug)

pub fn decode(image: Image, pc: Int) -> Instruction raise ImageError {
  let (op, mode) = decoding(image.read(pc)).unwrap_or_error(InvalidImage("Unsupported opcode at \{pc}"))
  let size = width(mode)
  let arg = if size == 1 { 0 } else if size == 2 { image.read(pc + 1) } else { image.word(pc + 1) }
  { pc, op, mode, arg, next: (pc + size) & 65535 }
}

pub(all) struct Dispatch {
  call_pc: Int
  helper: Int
  pointer: Int
  setup: Array[Instruction]
  targets: Array[Int]
} derive(Debug)

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

// Bounded partial execution proves the dispatcher for every table index. The
// incoming X/Y and RAM are unknown. Only values established by the helper or
// the real JSR return token may be read. We retain the helper's effects in C;
// this analysis proves target/continuation behavior, not their deadness.
fn dispatch_case(image: Image, helper: Int, return_pc: Int, index: Int) -> (Int, Int, Array[Instruction])? {
  let memory: Map[Int, Int] = Map([])
  let mut a = index
  let mut y: Int? = None
  let mut pops = 0
  let mut pc = helper
  let setup = []
  try {
    for _ in 0..<64 {
      let inst = decode(image, pc)
      match (inst.op, inst.mode) {
        (ASL, "acc") => a = (a << 1) & 255
        (TAY, "imp") => y = Some(a)
        (PLA, "imp") => {
          if pops >= 2 { return None }
          a = if pops == 0 { return_pc & 255 } else { return_pc >> 8 }
          pops += 1
        }
        (STA, "zp") => memory[inst.arg] = a
        (INY, "imp") => {
          if y is Some(v) { y = Some((v + 1) & 255) } else { return None }
        }
        (LDA, "indy") => {
          match (memory.get(inst.arg), memory.get((inst.arg + 1) & 255), y) {
            (Some(lo), Some(hi), Some(offset)) => a = image.read(((lo | (hi << 8)) + offset) & 65535)
            _ => return None
          }
        }
        (JMP, "ind") => {
          let high_address = (inst.arg & 0xff00) | ((inst.arg + 1) & 255)
          match (memory.get(inst.arg), memory.get(high_address)) {
            (Some(lo), Some(hi)) if pops == 2 => return Some((lo | (hi << 8), inst.arg, setup))
            _ => return None
          }
        }
        _ => return None
      }
      setup.push(inst)
      pc = inst.next
    }
    None
  } catch { _ => None }
}

fn dispatch_at(image: Image, call: Instruction, words: Map[Int, Int]) -> Dispatch? {
  let targets = []
  let mut pos = call.next
  while words.contains(pos) && targets.length() < 128 {
    targets.push(words[pos])
    pos += 2
  }
  if targets.is_empty() { return None }
  let mut setup = []
  let mut pointer = 0
  for index, target in targets {
    match dispatch_case(image, call.arg, call.next - 1, index) {
      Some((actual, indirect, instructions)) if actual == target => {
        pointer = indirect
        setup = instructions
      }
      _ => return None
    }
  }
  Some({ call_pc: call.pc, helper: call.arg, pointer, setup, targets })
}

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
  let words: Map[Int, Int] = Map([])
  for i, item in image.source {
    if item is Directive(name="dw", args~) {
      for j, _ in args { words[image.addresses[i] + 2 * j] = image.word(image.addresses[i] + 2 * j) }
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
      let target = canonical(image.word(address))
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
    let pc = canonical(address)
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
      let irq = canonical(image.word(canonical(0xfffe)))
      if !roots.contains(irq) { roots.push(irq); work.push((irq, Facts::unknown())) }
    }
    let after = transfer(inst, f)
    match (inst.op, inst.mode) {
      (JSR, _) => {
        match dispatch_at(image, inst, words) {
          Some(dispatch) => {
            dispatches[pc] = dispatch
            for target in dispatch.targets { work.push((target, Facts::unknown())) }
          }
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
        } else { raise InvalidImage("Unresolved indirect transfer at \{pc}") }
      }
      (RTS | RTI, _) => ()
      (BRK, _) => raise InvalidImage("BRK requires interrupt scheduling at \{pc}")
      (_, "rel") => {
        if condition(inst.op, f) != Some(false) { work.push((branch_target(inst), assume_condition(inst.op, true, after))) }
        if condition(inst.op, f) != Some(true) { work.push((inst.next, assume_condition(inst.op, false, after))) }
      }
      _ => work.push((inst.next, after))
    }
  }
  let needed: Set[Int] = Set(roots)
  for inst in instructions.values() {
    if inst.mode == "rel" { needed.add(branch_target(inst)) }
    if inst.op == JMP || inst.op == JSR { needed.add(inst.arg) }

  }
  for i, item in image.source {
    if item is Directive(name="db", ..) && instructions.contains(image.addresses[i]) {
      needed.add(instructions[image.addresses[i]].next)
    }
  }
  let source = []
  let addresses = []
  fn append(item: @parse.SourceItem, pc: Int) {
    source.push(item)
    addresses.push(pc)
  }
  // Source text remains intact. Only the compiler view substitutes decoded
  // instructions for executable .db bytes and inserts explicit overlap edges.
  for i, item in image.source {
    let pc = image.addresses[i]
    match item {
      Inst(_) => {
        if instructions.contains(pc) {
          if needed.contains(pc) && address_name(image, pc).has_prefix("loc_") { append(Label(address_name(image, pc)), pc) }
          let inst = instructions[pc]
          if inst.mode == "rel" && condition(inst.op, facts[pc]) == Some(true) {
            append(Inst(op=JMP, mode=AbsoluteOrZeroPage(addr=Ident(address_name(image, branch_target(inst))))), pc)
          } else if inst.mode != "rel" || condition(inst.op, facts[pc]) != Some(false) {
            append(item, pc)
          }
        }
      }
      Directive(name="db", ..) if instructions.contains(pc) => {
        let inst = instructions[pc]
        append(Label(address_name(image, pc)), pc)
        append(Inst(op=inst.op, mode=source_mode(image, inst)), pc)
        append(Inst(op=JMP, mode=AbsoluteOrZeroPage(addr=Ident(address_name(image, inst.next)))), pc)
      }
      _ => append(item, pc)
    }
  }
  { image, instructions, dispatches, roots: roots.map(fn(pc) { address_name(image, pc) }), facts, idle, source, addresses }
}
