// Source addresses are semantic identity. Labels and comments are views over
// the bytes, not boundaries that force a code/data partition or a C function.
//
// With a mapper, `bytes` holds the fixed banks from `origin` to $FFFF (see
// `Layout`), and the banks that switch into the windows are in `banks`.
// Code and data in a window are then located by bank as well as address: a
// location is `(bank + 1) << 16 | address` (see `bank_location`), and a
// window address without a bank is the window as mapped at run time.
// Locations outside the windows, and every location of a cartridge without
// banks, are plain addresses.
pub(all) struct Image {
  origin: Int
  bytes: Array[Int]
  symbols: Map[String, Int]
  addresses: Array[Int]
  source: Array[@parse.SourceItem]
  banks: Banks
  // Data a ROM map names (location and length): execution never decodes
  // it; a transfer into it traps.
  data: Array[(Int, Int)]
  // derived: the locations of `data`, and the source's constants
  data_cells: Set[Int]
  defines: Map[String, @parse.SourceExpr]
} derive(Debug)

pub fn Image::new(origin: Int, bytes: Array[Int], symbols: Map[String, Int], addresses: Array[Int],
                  source: Array[@parse.SourceItem], banks: Banks, data: Array[(Int, Int)]) -> Image {
  let data_cells: Set[Int] = Set([])
  for d in data {
    for i in 0..<d.1 { data_cells.add(d.0 + i) }
  }
  let defines: Map[String, @parse.SourceExpr] = Map([])
  for item in source {
    if item is Define(name~, rhs~) { defines[name] = rhs }
  }
  { origin, bytes, symbols, addresses, source, banks, data, data_cells, defines }
}

pub fn Image::is_data(self: Image, location: Int) -> Bool {
  self.data_cells.contains(location)
}

pub(all) struct Banks {
  contents: Array[Array[Int]] // `Layout.bank_size` bytes each
  homes: Array[Int] // the window each bank's code and tables are at (-1 when unknown)
  companions: Array[Int] // the bank always in the other window while each runs (-1 when none)
  names: Array[String] // prefixes of the banks' names
  windows: Map[Int, Array[Int]] // window address -> the banks the map names routines at it in
  window_names: Map[Int, String] // window address -> the routine as fixed-bank code calls it
  code: Array[(Int, Int, Int)] // locations of the code addresses in the map's code tables, what to add to them, and the table's address
  shadows: Array[(Int, Int)] // a window and the RAM byte holding its bank, where nothing else tells
  jumps: Array[(Int, Int)] // where computed jumps go besides the code tables: the jump's location and the target's
  jump_tables: Array[(Int, Int)] // the code tables computed jumps read (when not loaded just before): the jump's location and the table's address
  inline: Array[(Int, Int)] // routines that read bytes after their calls and return past them: location, bytes
  ram_code: Map[Int, Int] // code the program writes to RAM and runs there: address -> byte (the map's `ram`)
  high: Map[Int, Int] // split code tables: the location of a code address's low byte -> its high byte's
  waits: Array[Int] // transfers back that wait for the interrupt (the map's `wait`)
} derive(Debug)

pub fn Banks::none() -> Banks {
  { contents: [], homes: [], companions: [], names: [], windows: Map([]), window_names: Map([]), code: [], shadows: [], jumps: [], jump_tables: [], inline: [], ram_code: Map([]), high: Map([]), waits: [] }
}

// The code address in a code table at `location` (split tables keep the
// high bytes elsewhere).
pub fn Image::code_word(self: Image, location: Int) -> Int raise ImageError {
  match self.banks.high.get(location) {
    Some(high) => self.read(location) | (self.read(high) << 8)
    None => self.word(location)
  }
}

// Where a call returns: after it, or after the bytes that follow it when
// the routine reads them and returns past them (the map's `inline`).
pub fn Image::continuation(self: Image, call: Int, next: Int, target: Int) -> Int {
  match self.banks.inline.iter().find_first(fn(i) { i.0 == target || (i.0 & 0xffff) == target }) {
    Some((_, bytes)) => rebase(call, (next & 0xffff) + bytes)
    None => next
  }
}

pub fn Image::banked(self: Image) -> Bool {
  !self.banks.contents.is_empty()
}

pub fn bank_location(bank: Int, address: Int) -> Int {
  ((bank + 1) << 16) | address
}

// The switchable bank of a location, if it has one.
pub fn bank_of(location: Int) -> Int? {
  if location >= 0x10000 { Some((location >> 16) - 1) } else { None }
}

// How a mapper lays PRG ROM out at $8000-$FFFF: regions of `bank_size`
// bytes, each a switchable window or fixed to a bank.
//
//   MMC1: a 16 KiB window at $8000, the last bank fixed at $C000;
//   MMC3: 8 KiB banks, windows at $8000 and $A000 and the last two banks
//         fixed at $C000 and $E000, or (PRG mode 1) windows at $A000 and
//         $C000 and those banks fixed at $8000 and $E000.
//
// The image's helpers read the layout of the image last made (a ROM's, or
// none for NROM and assembly), so that locations need no image to resolve.
pub(all) struct Layout {
  bank_size: Int
  windows: Array[Int] // the windows' start addresses, ascending
  fixed: Array[(Int, Int)] // start address and bank of each fixed region
} derive(Debug, Eq)

pub fn Layout::none() -> Layout {
  { bank_size: 0x8000, windows: [], fixed: [] }
}

pub fn Layout::mmc1(banks: Int) -> Layout {
  { bank_size: 0x4000, windows: [0x8000], fixed: [(0xc000, banks - 1)] }
}

pub fn Layout::mmc3(banks: Int, prg_mode: Int) -> Layout {
  if prg_mode == 0 {
    { bank_size: 0x2000, windows: [0x8000, 0xa000], fixed: [(0xc000, banks - 2), (0xe000, banks - 1)] }
  } else {
    { bank_size: 0x2000, windows: [0xa000, 0xc000], fixed: [(0x8000, banks - 2), (0xe000, banks - 1)] }
  }
}

let current_layout: Ref[Layout] = Ref(Layout::none())

pub fn set_layout(layout: Layout) -> Unit {
  current_layout.val = layout
}

pub fn layout() -> Layout {
  current_layout.val
}

// The window an address is in, or -1.
pub fn window_of(address: Int) -> Int {
  let a = address & 0xffff
  let l = current_layout.val
  for i, start in l.windows {
    if a >= start && a < start + l.bank_size { return i }
  }
  -1
}

pub fn in_window(address: Int) -> Bool {
  window_of(address) >= 0
}

// The runtime variable holding the bank mapped in window `window`.
pub fn window_bank_variable(window: Int) -> String {
  if window == 0 { "prg_bank" } else { "prg_bank\{window}" }
}

// The fixed bank at an address, or -1.
pub fn fixed_bank(address: Int) -> Int {
  let a = address & 0xffff
  let l = current_layout.val
  for region in l.fixed {
    if a >= region.0 && a < region.0 + l.bank_size { return region.1 }
  }
  -1
}

// `address` as seen from code at `from`: addresses in the window that code
// is in are in its bank; those in another window are that window as mapped.
pub fn rebase(from: Int, address: Int) -> Int {
  let address = address & 0xffff
  if from >= 0x10000 {
    let window = window_of(address)
    if window >= 0 && window == window_of(from) { return (from & 0x7fff0000) | address }
  }
  address
}

pub suberror ImageError { InvalidImage(String) } derive(Debug)

fn value(expr: @parse.SourceExpr, symbols: Map[String, Int], defines: Map[String, @parse.SourceExpr], active: Set[String]) -> Int raise ImageError {
  match expr {
    Const(value~, ..) => value
    Ident(name) => {
      if active.contains(name) { raise InvalidImage("Cyclic definition: \{name}") }
      match defines.get(name) {
        Some(expr) => {
          active.add(name)
          let result = value(expr, symbols, defines, active)
          active.remove(name)
          result
        }
        None => symbols.get(name).unwrap_or_error(InvalidImage("Undefined symbol: \{name}"))
      }
    }
    Unary(op~, expr~) => {
      let v = value(expr, symbols, defines, active)
      match op { GetLowByte => v & 255; GetHighByte => (v >> 8) & 255 }
    }
    Binary(op~, lhs~, rhs~) => {
      let a = value(lhs, symbols, defines, active)
      let b = value(rhs, symbols, defines, active)
      match op { Add => a + b; Sub => a - b }
    }
  }
}

pub fn Image::eval(self: Image, expr: @parse.SourceExpr) -> Int raise ImageError {
  value(expr, self.symbols, self.defines, Set([]))
}

pub fn operand(mode: @parse.SourceAddressingMode) -> @parse.SourceExpr? {
  match mode {
    Implied | Accumulator => None
    Immediate(e) | Relative(addr=e) | AbsoluteOrZeroPage(addr=e) |
    AbsoluteOrZeroPageX(addr=e) | AbsoluteOrZeroPageY(addr=e) |
    Indirect(addr=e) | IndexedIndirect(addr=e) | IndirectIndexed(addr=e) => Some(e)
  }
}

fn form(op: @parse.OpCode, mode: @parse.SourceAddressingMode, resolved: Int?) -> String {
  let zp = match resolved { Some(v) => v >= 0 && v < 256; None => false }
  match mode {
    Implied => "imp"
    Accumulator => "acc"
    Immediate(_) => "imm"
    Relative(_) => "rel"
    Indirect(_) => "ind"
    IndexedIndirect(_) => "indx"
    IndirectIndexed(_) => "indy"
    AbsoluteOrZeroPage(_) => if zp && encoding(op, "zp") != None { "zp" } else { "abs" }
    AbsoluteOrZeroPageX(_) => if zp && encoding(op, "zpx") != None { "zpx" } else { "absx" }
    AbsoluteOrZeroPageY(_) => if zp && encoding(op, "zpy") != None { "zpy" } else { "absy" }
  }
}

pub fn width(form: String) -> Int {
  match form {
    "imp" | "acc" => 1
    "abs" | "absx" | "absy" | "ind" => 3
    _ => 2
  }
}

pub fn assemble(source: Array[@parse.SourceItem]) -> Image raise ImageError {
  let symbols: Map[String, Int] = Map([])
  let defines: Map[String, @parse.SourceExpr] = Map([])
  for item in source {
    if item is Define(name~, rhs~) {
      if defines.contains(name) { raise InvalidImage("Duplicate definition: \{name}") }
      defines[name] = rhs
    }
  }
  let addresses = Array::make(source.length(), 0)
  let forms = Array::make(source.length(), "")
  let mut origin = 0x10000
  let mut end = 0
  let mut converged = false
  // Relax zero-page encodings until forward labels and instruction sizes agree.
  for _ in 0..<32 {
    let mut pc = 0
    let mut changed = false
    origin = 0x10000
    end = 0
    let seen: Set[String] = Set([])
    for i, item in source {
      addresses[i] = pc
      match item {
        Label(name) => {
          if seen.contains(name) || defines.contains(name) { raise InvalidImage("Duplicate label: \{name}") }
          seen.add(name)
          if symbols.get(name) != Some(pc) { symbols[name] = pc; changed = true }
        }
        Inst(op~, mode~) => {
          let resolved = match operand(mode) {
            Some(expr) => try Some(value(expr, symbols, defines, Set([]))) catch { _ => None }
            None => None
          }
          let kind = form(op, mode, resolved)
          if encoding(op, kind) == None { raise InvalidImage("Unsupported encoding: \{@debug.to_string(op)} \{kind}") }
          if forms[i] != kind { forms[i] = kind; changed = true }
          origin = math_min(origin, pc)
          pc += width(kind)
          end = math_max(end, pc)
        }
        Directive(name="org", args~) => {
          if args.length() != 1 { raise InvalidImage(".org requires one address") }
          pc = value(args[0], symbols, defines, Set([]))
          if pc < 0 || pc > 65535 { raise InvalidImage("Invalid origin") }
          addresses[i] = pc
        }
        Directive(name="db" | "dw" as name, args~) => {
          origin = math_min(origin, pc)
          pc += args.length() * (if name == "dw" { 2 } else { 1 })
          end = math_max(end, pc)
        }
        Directive(name="index" | "mem", ..) | Define(_) | Comment(_) => ()
        Directive(name~, ..) => raise InvalidImage("Unsupported directive: \{name}")
      }
    }
    if !changed { converged = true; break }
  }
  if !converged || origin > end || end > 65536 { raise InvalidImage("Assembly layout did not converge in the 16-bit address space") }
  let bytes = Array::make(end - origin, 0)
  let written: Set[Int] = Set([])
  fn put(address: Int, v: Int) -> Unit raise ImageError {
    if written.contains(address) { raise InvalidImage("Overlapping .org output at \{address}") }
    written.add(address)
    bytes[address - origin] = v & 255
  }
  for i, item in source {
    let pc = addresses[i]
    match item {
      Inst(op~, mode~) => {
        let kind = forms[i]
        put(pc, encoding(op, kind).unwrap())
        if operand(mode) is Some(expr) {
          let v = value(expr, symbols, defines, Set([]))
          if kind == "rel" {
            let delta = v - (pc + 2)
            if delta < -128 || delta > 127 { raise InvalidImage("Branch out of range at \{pc}: \{delta}") }
            put(pc + 1, delta)
          } else {
            if v < 0 || v >= (if width(kind) == 2 { 256 } else { 65536 }) { raise InvalidImage("Operand out of range at \{pc}: \{v}") }
            put(pc + 1, v)
            if width(kind) == 3 { put(pc + 2, v >> 8) }
          }
        }
      }
      Directive(name="db" | "dw" as name, args~) => {
        let size = if name == "dw" { 2 } else { 1 }
        for j, expr in args {
          let v = value(expr, symbols, defines, Set([]))
          if v < 0 || v >= (if size == 1 { 256 } else { 65536 }) { raise InvalidImage("Data out of range at \{pc}: \{v}") }
          put(pc + j * size, v)
          if size == 2 { put(pc + j * size + 1, v >> 8) }
        }
      }
      _ => ()
    }
  }
  set_layout(Layout::none())
  Image::new(origin, bytes, symbols, addresses, source, Banks::none(), [])
}

fn math_min(a: Int, b: Int) -> Int { if a < b { a } else { b } }
fn math_max(a: Int, b: Int) -> Int { if a > b { a } else { b } }

pub fn Image::read(self: Image, address: Int) -> Int raise ImageError {
  if address < 0x8000 {
    // code the program writes to RAM (not the bytes it changes)
    return match self.banks.ram_code.get(address) {
      Some(byte) if byte >= 0 => byte
      Some(_) => raise InvalidImage("The program changes the byte at \{show_pc(address)}")
      None => raise InvalidImage("Address outside image: \{address}")
    }
  }
  if self.banked() {
    let a = address & 0xffff
    if in_window(a) {
      guard bank_of(address) is Some(bank) && bank < self.banks.contents.length() else {
        raise InvalidImage("Address in a switchable window without a bank: \{show_pc(a)}")
      }
      return self.banks.contents[bank][a & (layout().bank_size - 1)]
    }
    return self.bytes.get(a - self.origin).unwrap_or_error(InvalidImage("Address outside image: \{a}"))
  }
  let index = if self.bytes.length() == 16384 && self.origin >= 0x8000 && address >= 0x8000 {
    (address - 0x8000) & 0x3fff
  } else { address - self.origin }
  self.bytes.get(index).unwrap_or_error(InvalidImage("Address outside image: \{address}"))
}

pub fn Image::word(self: Image, address: Int) -> Int raise ImageError {
  self.read(address) | (self.read(rebase(address, (address & 0xffff) + 1)) << 8)
}
