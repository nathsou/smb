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
  // Where the program replaces the stack pointer outside the reset routine
  // (`ldx #$ff; txs`, then on with the reset routine's code: a game that
  // restarts from deep in its calls). The stack analysis finds them; the IR
  // unwinds the calls there and the reset routine continues (see the IR's
  // restart.mbt).
  restarts: Array[Int]
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

// The location a JSR or JMP leads to: a window address is in the caller's
// bank, or the window as mapped, unless the explorer proved which bank is
// mapped there (then the operand is that bank's location, see `resolve`).
pub fn jump_target(inst: Instruction) -> Int {
  if inst.arg >= 0x10000 { inst.arg } else { rebase(inst.pc, inst.arg) }
}

// Whether discovery follows banks (MMC3: two windows).
fn follows_banks() -> Bool {
  layout().windows.length() >= 2
}

// A call or jump into a window from outside it, when the bank mapped there
// is known: to that bank's routine instead of the window as mapped.
fn resolve(image: Image, inst: Instruction, f: Facts) -> Instruction {
  if !(inst.op == JSR || (inst.op == JMP && inst.mode == "abs")) { return inst }
  let target = rebase(inst.pc, inst.arg & 0xffff)
  match f.banking {
    Some(b) if target < 0x10000 && in_window(target) =>
      match b.mapped(window_of(target), image.banks.shadows) {
        // (a bank the map does not name there and without code there is a
        // path the program does not take: the window decides)
        [bank] if bank < image.banks.contents.length() &&
          (image.banks.windows.get(target).unwrap_or([]).contains(bank) || candidate(image, bank, target)) =>
          { ..inst, arg: bank_location(bank, target) }
        _ => { ..inst, arg: inst.arg & 0xffff }
      }
    _ => inst
  }
}

// Whether bank `bank` can be what a window holds when code reaches `pc`
// there: made for that window (when the map says), with bytes there that
// look like code.
fn candidate(image: Image, bank: Int, pc: Int) -> Bool {
  bank < image.banks.contents.length() &&
  (image.banks.homes[bank] < 0 || image.banks.homes[bank] == window_of(pc)) &&
  fixed_bank_number(bank) < 0 &&
  looks_like_code(image, bank_location(bank, pc))
}

// The fixed region a bank is fixed in, or -1 (fixed banks never switch in).
fn fixed_bank_number(bank: Int) -> Int {
  match layout().fixed.iter().find_first(fn(r) { r.1 == bank }) {
    Some(r) => r.0
    None => -1
  }
}

// Whether a routine pulls its return address first thing (a dispatcher
// reading the table after its call), before pushing anything.
fn reads_return_address(image: Image, entry: Int) -> Bool {
  let mut pc = entry
  for _ in 0..<8 {
    let inst = decode(image, pc) catch { _ => return false }
    match inst.op {
      PLA => return true
      PHA | PHP | JSR | JMP | RTS | RTI | BRK => return false
      _ => if inst.mode == "rel" { return false }
    }
    pc = inst.next
  }
  false
}

// Whether a location looks like code: the code it reaches in its bank and
// the fixed banks (branches, jumps, calls, but not past a call that a table
// of the map follows) decodes, for a while, without BRK or an opcode the
// 6502 lacks. Data runs into one soon; games' code has none.
let code_looks: Map[Int, Bool] = Map([])

fn looks_like_code(image: Image, location: Int) -> Bool {
  match code_looks.get(location) {
    Some(known) => return known
    None => ()
  }
  let seen: Set[Int] = Set([])
  // with what is known of the flags, to skip branches always taken (over
  // data, say)
  let work = [(location, Facts::unknown())]
  let mut verdict = true
  while verdict && seen.length() < 512 && work.pop() is Some((pc, f)) {
    if seen.contains(pc) || image.is_data(pc) { continue }
    // only the bank of the location, and the fixed banks
    if in_window(pc) && (pc < 0x10000 || bank_of(pc) != bank_of(location)) { continue }
    seen.add(pc)
    let inst = decode(image, pc) catch { _ => { verdict = false; continue } }
    let after = transfer(inst, f)
    match inst.op {
      BRK => verdict = false
      RTS | RTI => ()
      JMP => if inst.mode == "abs" { work.push((jump_target(inst), after)) }
      JSR => {
        work.push((jump_target(inst), Facts::unknown()))
        if !image.data.iter().any(fn(d) { d.0 == inst.next }) && !reads_return_address(image, jump_target(inst)) {
          work.push((inst.next, after))
        }
      }
      _ =>
        if inst.mode == "rel" {
          match condition(inst.op, f) {
            Some(true) => work.push((branch_target(inst), after))
            Some(false) => work.push((inst.next, after))
            None => {
              work.push((branch_target(inst), after))
              work.push((inst.next, after))
            }
          }
        } else {
          work.push((inst.next, after))
        }
    }
  }
  code_looks[location] = verdict
  verdict
}

// The bytes an indexed load at `pc` can read from ROM: those of the map's
// table there, from its base to the table's end (none without a table).
fn table_values(image: Image, inst: Instruction) -> Values {
  let location = rebase(inst.pc, inst.arg)
  match image.data.iter().find_first(fn(d) { location >= d.0 && location < d.0 + d.1 }) {
    Some(d) => {
      let bytes = []
      for at in location..<(d.0 + d.1) {
        bytes.push(image.read(at) catch { _ => return [] })
      }
      values_of(bytes)
    }
    None => []
  }
}

// What a call leaves known of the banks, by evaluating the callee when it
// is short and straight (a routine that switches banks), else nothing.
fn call_effect(image: Image, target: Int, f: Facts, nesting: Int) -> Banking? {
  let mut pc = target
  let mut facts = { ..f, depth: Some(0) }
  for _ in 0..<48 {
    let inst = resolve(image, decode(image, pc) catch { _ => return None }, facts)
    match inst.op {
      RTS => return if facts.depth == Some(0) { facts.banking } else { None }
      JSR => {
        if nesting >= 3 { return None }
        guard call_effect(image, jump_target(inst), facts, nesting + 1) is Some(b) else { return None }
        facts = { ..transfer(inst, facts), banking: Some(b) }
        pc = inst.next
      }
      JMP if inst.mode == "abs" => pc = jump_target(inst)
      JMP | RTI | BRK => return None
      _ if inst.mode == "rel" =>
        match condition(inst.op, facts) {
          Some(true) => pc = branch_target(inst)
          Some(false) => pc = inst.next
          None => return None
        }
      _ => {
        facts = transfer(inst, facts, table=fn(_) { table_values(image, inst) })
        pc = inst.next
      }
    }
  }
  None
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

// Whether an RTS returns through two bytes pushed above a return address
// to the next instruction (`lda #>rts; pha; lda #<rts; pha`, then the
// target's address minus one, pushed, and the RTS): a computed call, which
// comes back after the RTS.
fn pushes_return(image: Image, site: Int) -> Bool {
  let at = site & 0xffff
  for back in 8..=32 {
    let start = at - back
    let bytes = []
    for i in 0..<6 {
      bytes.push(try image.read(rebase(site, start + i)) catch { _ => -1 })
    }
    if bytes[0] == 0xa9 && bytes[2] == 0x48 && bytes[3] == 0xa9 && bytes[5] == 0x48 &&
      ((bytes[1] << 8) | bytes[4]) == at {
      return true
    }
  }
  false
}

// Whether the computed jump at `site` is a computed call (see
// `pushes_return`): it comes back to the instruction after it.
pub fn Program::computed_call(self: Program, site: Int) -> Bool {
  match (self.instructions.get(site), self.facts.get(site)) {
    (Some(inst), Some(f)) => inst.op == RTS && f.depth == Some(4) && self.jumps.contains(site)
    _ => false
  }
}

// Where a computed jump at `site` (through RAM, or returning through an
// address the routine pushed) can go: the addresses in the map's code tables
// of its bank (the fixed bank's for code in the fixed bank).
//
// Following banks (MMC3), a jump in a bank can also read the code tables of
// banks in the other window, as mapped: their addresses are that window's,
// whichever of those banks is there (see the window's dispatcher).
fn code_targets(image: Image, site: Int, call~: Bool = false) -> Array[Int] raise ImageError {
  let targets = []
  // the tables the pointer is loaded from (lda table,x or lda table,y just
  // before the jump), when the map names them: those alone
  let loaded = []
  for back in 3..=24 {
    let at = (site & 0xffff) - back
    let op = try image.read(rebase(site, at)) catch { _ => continue }
    if op == 0xb9 || op == 0xbd {
      let operand = try image.word(rebase(site, at + 1)) catch { _ => continue }
      loaded.push(operand)
    }
  }
  // (or, for a computed call when none starts there, read from just before
  // one: indexed from 1 or more)
  let exact = image.banks.code.filter(fn(code) { loaded.contains(code.2) || loaded.contains(code.2 + 1) })
  let tables = if !exact.is_empty() || !call { exact } else {
    image.banks.code.filter(fn(code) { loaded.iter().any(fn(l) { l < code.2 && l >= code.2 - 8 }) })
  }
  let from_loads = !tables.is_empty()
  // (a computed call only goes where its table says)
  let entries = if from_loads || call { tables } else { image.banks.code }
  // a bank without code tables of its own jumps to the other window's
  let own = image.banks.code.iter().any(fn(code) { bank_of(code.0) == bank_of(site) })
  for code in entries {
    let (location, adjust, _) = code
    let other_window = follows_banks() && (from_loads || !own) && bank_of(site) is Some(_) && bank_of(location) is Some(_) &&
      window_of(location) != window_of(site)
    if bank_of(location) != bank_of(site) && !other_window { continue }
    let word = image.word(location)
    if word < 0x8000 { continue } // an unused entry
    let target = rebase(location, word + adjust)
    let target = if other_window && bank_of(target) is Some(_) { target & 0xffff } else { target }
    if !targets.contains(target) { targets.push(target) }
  }
  targets
}

// Discovery stops at the first problem. The inspector can collect them
// instead (`collect_problems`): those discovery can step over are recorded,
// and exploration goes on without what they lead to.
let collected: Ref[Array[String]?] = Ref(None)

pub fn collect_problems() -> Unit {
  collected.val = Some([])
}

pub fn problems() -> Array[String] {
  collected.val.unwrap_or([])
}

// Records a problem when collecting (and returns true), else raises it.
fn problem(message: String) -> Bool raise ImageError {
  match collected.val {
    Some(list) => {
      if !list.contains(message) { list.push(message) }
      true
    }
    None => raise InvalidImage(message)
  }
}

// Follows execution from the reset and NMI vectors. Inline tables after a
// call are the data directives in `image.source` that start at its return
// address (from the assembly source, or from a ROM map).
//
// Following banks, the interrupts switch them where the foreground waits:
// what is known of them is forgotten there (but the shadows). A call keeps
// what it knows of the windows its callee never switches. The wait loops
// and the windows each routine switches come from the exploration before,
// until they settle (the first assumes every call switches everything).
fn explore(image: Image) -> Exploration raise ImageError {
  // both only grow, so that they settle; the last exploration assumed all
  // it found
  let waits: Map[Int, Int] = Map([])
  let mut switching: Map[Int, Int]? = None
  if !follows_banks() { return explore_with(image, waits, switching) }
  // problems count in the last exploration only: the ones before knew less
  let collecting = collected.val is Some(_)
  fn settled(result: Exploration) -> Exploration raise ImageError {
    if !collecting {
      let found = problems()
      collected.val = None
      if found.length() > 0 { raise InvalidImage(found[0]) }
    }
    result
  }
  for _ in 0..<16 {
    collected.val = Some([])
    let result = explore_with(image, waits, switching)
    let next = switching_reasons(flow_of(result), image.banks.shadows).0
    let known_waits = result.waits.iter().all(fn(w) { waits.get(w.0) == Some(w.1) })
    match switching {
      Some(known) => {
        let covered = next.iter().all(fn(r) { (known.get(r.0).unwrap_or(0) | r.1) == known.get(r.0).unwrap_or(0) })
        if known_waits && covered { return settled(result) }
        for entry, mask in next { known[entry] = known.get(entry).unwrap_or(0) | mask }
      }
      None => switching = Some(next)
    }
    for site, start in result.waits { waits[site] = start }
  }
  if !collecting { collected.val = None }
  raise InvalidImage("The wait loops and bank switches do not settle while following banks")
}

// Why a routine of the program switches banks, for the inspector: the chain
// of calls to a write to the mapper or a shadow.
pub fn Program::switching_chain(self: Program, entry: Int) -> Array[String] {
  let flow = { instructions: self.instructions, dispatches: self.dispatches, jumps: self.jumps, windows: self.windows, facts: self.facts }
  let (switching, reasons) = switching_reasons(flow, self.image.banks.shadows)
  let chain = []
  let mut at = entry
  while switching.contains(at) && chain.length() < 20 {
    match reasons.get(at) {
      Some((write, callee)) => {
        let windows = "windows \{switching[at]}"
        chain.push(show_pc(at) + " (\{windows})" + (if callee < 0 { " writes at " + show_pc(write) } else { " calls " + show_pc(callee) + " at " + show_pc(write) }))
        if callee < 0 { break }
        at = callee
      }
      None => break
    }
  }
  chain
}

priv struct Flow {
  instructions: Map[Int, Instruction]
  dispatches: Map[Int, Dispatch]
  jumps: Map[Int, Array[Int]]
  windows: Map[Int, Array[Int]]
  facts: Map[Int, Facts]
}

fn flow_of(e: Exploration) -> Flow {
  { instructions: e.instructions, dispatches: e.dispatches, jumps: e.jumps, windows: e.windows, facts: e.facts }
}

// The windows (a bit each) an instruction may switch: a bank register
// written (which the bank select known there names, else any), or a
// shadow. (Stores through pointers, and indexed from below $8000, are taken
// not to reach the mapper.)
fn switched_windows(inst: Instruction, f: Facts?, shadows: Array[(Int, Int)]) -> Int {
  let store = inst.op == STA || inst.op == STX || inst.op == STY || inst.op == INC ||
    inst.op == DEC || inst.op == ASL || inst.op == LSR || inst.op == ROL || inst.op == ROR
  if !store { return 0 }
  let all = (1 << layout().windows.length()) - 1
  let direct = inst.mode == "zp" || inst.mode == "abs"
  if direct {
    match shadows.iter().find_first(fn(s) { s.1 == inst.arg }) {
      Some(s) => return 1 << s.0
      None => ()
    }
  }
  let mapper = (inst.mode == "abs" || inst.mode == "absx" || inst.mode == "absy") && inst.arg >= 0x8000 && inst.arg < 0xa000
  if !mapper { return 0 }
  if inst.mode != "abs" { return all }
  if (inst.arg & 1) == 0 { return 0 } // the bank select itself
  let select = match f { Some({ banking: Some(b), .. }) => b.select; _ => None }
  match select {
    Some(value) => {
      let register = value & 7
      if register < 6 { return 0 }
      let start = if register == 7 { 0xa000 } else if (value & 0x40) != 0 { 0xc000 } else { 0x8000 }
      let window = window_of(start)
      if window < 0 { all } else { 1 << window }
    }
    None => all
  }
}

// The routines (called locations) that may switch a PRG bank, with the
// windows they switch: their code writes MMC3's bank registers or a shadow,
// or calls one that does. For each, where: a write (callee -1) or a call.
fn switching_reasons(flow: Flow, shadows: Array[(Int, Int)]) -> (Map[Int, Int], Map[Int, (Int, Int)]) {
  let e = flow
  // the code a routine runs without returning: its own, and what it jumps to
  let bodies: Map[Int, (Int, Int, Array[(Int, Int)])] = Map([]) // windows written, a write, calls (site, callee)
  fn body(entry: Int) -> (Int, Int, Array[(Int, Int)]) {
    match bodies.get(entry) {
      Some(b) => return b
      None => ()
    }
    let seen: Set[Int] = Set([])
    let work = [entry]
    let calls = []
    let mut mask = 0
    let mut write = -1
    while work.pop() is Some(pc) {
      if seen.contains(pc) { continue }
      seen.add(pc)
      match e.windows.get(pc) {
        Some(targets) => {
          for t in targets { work.push(t) }
          continue
        }
        None => ()
      }
      guard e.instructions.get(pc) is Some(inst) else { continue }
      let windows = switched_windows(inst, e.facts.get(pc), shadows)
      if windows != 0 {
        mask = mask | windows
        if write < 0 { write = pc }
      }
      match inst.op {
        JSR => {
          match e.dispatches.get(pc) {
            Some(d) => for t in d.targets { work.push(t) }
            None => {
              calls.push((pc, jump_target(inst)))
              work.push(inst.next)
            }
          }
        }
        JMP if inst.mode == "abs" => work.push(jump_target(inst))
        JMP => for t in e.jumps.get(pc).unwrap_or([]) { work.push(t) }
        RTS => for t in e.jumps.get(pc).unwrap_or([]) { work.push(t) }
        RTI | BRK => ()
        _ => {
          if inst.mode == "rel" { work.push(branch_target(inst)) }
          work.push(inst.next)
        }
      }
    }
    bodies[entry] = (mask, write, calls)
    (mask, write, calls)
  }
  let entries: Set[Int] = Set([])
  for _, inst in e.instructions {
    if inst.op == JSR && !e.dispatches.contains(inst.pc) { entries.add(jump_target(inst)) }
  }
  let switching: Map[Int, Int] = Map([])
  let reasons: Map[Int, (Int, Int)] = Map([])
  let mut changed = true
  while changed {
    changed = false
    for entry in entries {
      let (own, write, calls) = body(entry)
      let mut mask = own
      let mut reason = if own != 0 { Some((write, -1)) } else { None }
      for call in calls {
        let callee = switching.get(call.1).unwrap_or(0)
        if callee != 0 {
          mask = mask | callee
          if reason is None { reason = Some(call) }
        }
      }
      if mask != switching.get(entry).unwrap_or(0) {
        switching[entry] = mask
        reasons[entry] = reason.unwrap()
        changed = true
      }
    }
  }
  (switching, reasons)
}

fn explore_with(image: Image, waiting: Map[Int, Int], switching: Map[Int, Int]?) -> Exploration raise ImageError {
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
  // the dispatchers proven at each call (and its target), once
  let proofs: Map[(Int, Int), Dispatch?] = Map([])
  // the first transfer to each location, to explain how bad code was reached
  let parents: Map[Int, Int] = Map([])
  fn path(pc: Int) -> String {
    let steps = []
    let mut at = pc
    while steps.length() < 4 && parents.get(at) is Some(p) && p >= 0 {
      steps.push(show_pc(p))
      at = p
    }
    steps.join(" <- ")
  }
  let start = if follows_banks() { { ..Facts::unknown(), banking: Some(Banking::unknown()) } } else { Facts::unknown() }
  // the transfer back to a wait loop's start: the interrupts run there
  fn crossing(pc: Int, target: Int, facts: Facts) -> Facts {
    if waiting.get(pc) == Some(target) { { ..facts, banking: facts.banking.map(fn(b) { b.kept(image.banks.shadows, switched=-1, keep_banks=false) }) } } else { facts }
  }
  let work = roots.map(fn(pc) { (pc, start.entry(), -1) })
  while !work.is_empty() {
    let (address, incoming, from) = work.pop().unwrap()
    let pc = address
    if !parents.contains(pc) { parents[pc] = from }
    if image.banked() && pc < 0x10000 && in_window(pc) {
      // Code outside a window reaches it: whichever bank is mapped, among
      // those it can hold there (from what is known of the banks), else
      // among the routines the map names at this address.
      let derived = match incoming.banking {
        Some(b) => b.mapped(window_of(pc), image.banks.shadows)
        None => []
      }
      // the map, when it names routines here, says which of them can be;
      // else the banks whose bytes there look like code (the game never
      // calls into data; if it did, the dispatcher traps)
      let named = image.banks.windows.get(pc)
      let derived = match (named, derived) {
        // a bank known exactly, whose bytes there look like code
        (Some(banks), [bank]) if !banks.contains(bank) && candidate(image, bank, pc) => [bank]
        (Some(banks), _) => derived.filter(fn(b) { banks.contains(b) })
        (None, _) => derived.filter(fn(b) { candidate(image, b, pc) })
      }
      let banks = if !derived.is_empty() { derived } else {
        match named {
          Some(banks) => banks
          None => {
            let plausible = Array::makei(image.banks.contents.length(), fn(b) { b }).filter(fn(b) { candidate(image, b, pc) })
            problem("Code outside the window reaches \{show_pc(pc)} in a switchable window, from \{path(pc)}, but the map names no routine there in any bank (routine BANK:\{show_pc(pc)} NAME; banks with code-like bytes there: \{plausible.map(fn(b) { b.to_string() }).join(" ")})") |> ignore
            continue
          }
        }
      }
      let targets = windows.get(pc).unwrap_or([])
      for bank in banks {
        if bank >= image.banks.contents.length() { continue }
        let target = bank_location(bank, pc)
        if !targets.contains(target) { targets.push(target) }
        work.push((target, { ..start, banking: incoming.banking }.entry(), pc))
      }
      windows[pc] = targets
      continue
    }
    let inside = if image.banked() {
      (bank_of(pc) is Some(bank) && bank < image.banks.contents.length()) || fixed_bank(pc) >= 0
    } else { pc >= image.origin && pc < image.origin + image.bytes.length() }
    if !inside {
      problem("Execution outside the source address window at \{show_pc(pc)}, reached from \{path(pc)}; mirrored code needs an explicit source view") |> ignore
      continue
    }
    if image.is_data(pc) {
      // A path the program never takes when its tables hold what they
      // should: it traps rather than run data.
      traps.add(pc)
      continue
    }
    // code of a bank runs while its bank is mapped
    let incoming = match (incoming.banking, bank_of(pc)) {
      (Some(b), Some(bank)) => {
        let b = b.running(window_of(pc), bank)
        // and its companion in the other window, unless the code switched
        // that one to a bank known
        let companion = image.banks.companions.get(bank).unwrap_or(-1)
        let other = 1 - window_of(pc)
        let b = if companion >= 0 && b.bank(other).length() != 1 { b.running(other, companion) } else { b }
        { ..incoming, banking: Some(b) }
      }
      _ => incoming
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
    let decoded = decode(image, pc) catch {
      InvalidImage(message) => {
        problem("\{message}, reached from \{path(pc)}") |> ignore
        facts.remove(pc)
        continue
      }
    }
    let inst = resolve(image, decoded, f)
    instructions[pc] = inst
    // MMC3's scanline counter raises the IRQ once code enables interrupts
    // (the runtime raises it for no other mapper)
    if follows_banks() && (inst.op == CLI || inst.op == PLP) {
      let irq = image.word(canonical(0xfffe))
      if !roots.contains(irq) { roots.push(irq); work.push((irq, start.entry(), pc)) }
    }
    let after = transfer(inst, f, table=fn(_) { table_values(image, inst) })
    let after = match (inst.op, f.banking) {
      (JSR, Some(known)) =>
        match call_effect(image, jump_target(inst), f, 0) {
          Some(b) => { ..after, banking: Some(b) }
          None =>
            match switching {
              // the windows the callee never switches keep their banks
              Some(masks) =>
                { ..after, banking: Some(known.kept(image.banks.shadows, switched=masks.get(jump_target(inst)).unwrap_or(0))) }
              None => after
            }
        }
      _ => after
    }
    match (inst.op, inst.mode) {
      (JSR, _) => {
        let table = data_at.get(inst.next).map(fn(i) { inline_data_end(image, i) })
        let proven = match table {
          Some(end) =>
            match proofs.get((pc, inst.arg)) {
              Some(known) => Some(known)
              None => {
                let found = dispatch_at(image, inst, end)
                proofs[(pc, inst.arg)] = found
                Some(found)
              }
            }
          None => None
        }
        match proven {
          Some(Some(dispatch)) => {
            dispatches[pc] = dispatch
            // the targets run with what the call knows of the banks
            for target in dispatch.targets { work.push((target, { ..start, banking: f.banking }.entry(), pc)) }
          }
          // a callee that returns past this routine: whatever follows the
          // call is not reached from it
          _ if always_unwinds(image, jump_target(inst)) => work.push((jump_target(inst), f.entry(), pc))
          // Returning into data is never decoded as a guess.
          Some(None) => {
            problem("Call at \{show_pc(pc)} returns into inline data at \{show_pc(inst.next)}, but \{address_name(image, jump_target(inst))} was not proven to be an inline dispatcher") |> ignore
            work.push((jump_target(inst), f.entry(), pc))
          }
          None => {
            work.push((jump_target(inst), f.entry(), pc)); work.push((inst.next, after, pc))
          }
        }
      }
      (JMP, "abs") => {
        if jump_target(inst) == pc { idle.add(pc) } else { work.push((jump_target(inst), crossing(pc, jump_target(inst), after), pc)) }
      }
      (JMP, "ind") => {
        // Immutable ROM vectors are resolvable without a dispatcher summary;
        // pointers in RAM can reach the map's code addresses.
        match rom_vector(image, inst) {
          Some(target) => work.push((target, after, pc))
          None => {
            let targets = code_targets(image, pc)
            if targets.is_empty() {
              problem("Unresolved indirect transfer at \{show_pc(pc)}, reached from \{path(pc)}") |> ignore
              continue
            }
            jumps[pc] = targets
            // a jump keeps the banks mapped
            for target in targets { work.push((target, { ..start, banking: f.banking }.entry(), pc)) }
          }
        }
      }
      (RTS, _) if f.depth == Some(4) && pushes_return(image, pc) => {
        // a computed call, which returns after the RTS
        let targets = code_targets(image, pc, call=true)
        if targets.is_empty() {
          problem("Unresolved computed call at \{show_pc(pc)}, reached from \{path(pc)}") |> ignore
          continue
        }
        jumps[pc] = targets
        for target in targets { work.push((target, { ..start, banking: f.banking }.entry(), pc)) }
        work.push((inst.next, { ..Facts::unknown(), depth: Some(0), banking: f.banking.map(fn(_) { Banking::unknown() }) }, pc))
      }
      (RTS, _) if f.depth == Some(2) => {
        // returning through an address the routine pushed: a computed jump
        let targets = code_targets(image, pc)
        if !targets.is_empty() {
          jumps[pc] = targets
          for target in targets { work.push((target, { ..start, banking: f.banking }.entry(), pc)) }
        }
      }
      (RTS | RTI, _) => ()
      (BRK, _) => problem("BRK requires interrupt scheduling at \{show_pc(pc)}, reached from \{path(pc)}") |> ignore
      (_, "rel") => {
        if condition(inst.op, f) != Some(false) { work.push((branch_target(inst), crossing(pc, branch_target(inst), assume_condition(inst.op, true, after)), pc)) }
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
          None => for target in jumps.get(pc).unwrap_or([]) { needed.add(target) }
        }
      } else { needed.add(jump_target(inst)) }
    }
    if following.get(pc) != Some(inst.next) { needed.add(inst.next) }
    // where a restart would continue the reset routine (see `restarts`)
    if inst.op == TXS { needed.add(pc) }
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
        let call = inst.op == RTS && facts.get(pc).map(fn(f) { f.depth == Some(4) }).unwrap_or(false) && jumps.contains(pc)
        let falls = (call || (inst.op != JMP && inst.op != RTS)) && inst.op != RTI &&
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
  { image, instructions, dispatches, roots: roots.map(fn(pc) { address_name(image, pc) }), facts, idle, windows, jumps, waits, exact: Ref(false), restarts: [], source, addresses, map: RomMap::empty() }
}
