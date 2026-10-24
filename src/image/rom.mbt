// Recompiling from a ROM image and a map instead of an assembly source.
//
// The map records facts about the program and the names we give them:
//
//   routine $8000 reset                 ; optional description
//   table   $805a vram_lists_low 19      (length in bytes)
//   var     $0770 game_mode              (RAM; optional length)
//   io      $4015 apu_status             (hardware register)
//   enum    game_mode title=0 playing=1  (named values of a variable)
//   flags   button a=$80 b=$40           (named bits of a variable)
//   var     $0770 game_mode : game_mode  (a variable of that type)
//
// Cartridges with a mapper (MMC1) switch a 16 KiB bank of PRG ROM in at
// $8000-$BFFF while the last bank stays at $C000-$FFFF. The map names the
// banks and qualifies addresses in the window with the banks they are in:
//
//   bank    1 brinstar                   (a name for bank 1's entries)
//   routine 1-5:$80b0 update_objects     (in banks 1 to 5)
//   table   0:$9560 rooms 40             (in bank 0)
//   code    $c51f area_inits 6           (6 code addresses, a word each)
//   code    0-5:$b290 sound_defaults 9 5 (9 code addresses, 5 bytes apart)
//   code    1-5:$8048 moves 8 2 rts      (addresses minus one, for RTS)
//
// An entry in banks becomes one per bank, named after the bank
// (brinstar_update_objects), and one for the window as fixed-bank code sees
// it (update_objects): a call to it goes to the routine of the bank mapped
// at that moment, among those the map names; reading the table reads the
// bank mapped. A code table lists where computed jumps can go, through a
// pointer in RAM or by returning through an address the routine pushed: a
// jump in a bank may reach the addresses of that bank's code tables, a jump
// in the fixed bank those of the fixed bank's.
//
// Routines keep their names, which become C functions. Data names (tables,
// RAM and registers) become address constants and are upper-cased so they
// cannot collide with functions, struct fields or library names. A type's
// values are constants named after the type and the value (GAME_MODE_TITLE,
// BUTTON_A).
//
// Code is found by following execution from the vectors, as for assembly
// input. Inline tables after a call (jump tables) must be listed as tables,
// since nothing else marks where the instruction stream stops.

pub(all) enum MapKind {
  Routine
  Table
  Var
  Io
  Code // a table of code addresses
} derive(Debug, Eq)

pub(all) struct MapEntry {
  kind: MapKind
  address: Int
  name: String
  length: Int // bytes; 0 when not given
  type_name: String // a type of the map for its bytes; "" when not given
  description: String
  bank: Int // the switchable bank of a window address; -1 for the window as mapped, and outside it
  stride: Int // bytes from one code address to the next (code tables)
  adjust: Int // added to code addresses: 1 when they are pushed for RTS
} derive(Debug)

// Names for the values of a variable: an enumeration, or bits that combine.
pub(all) struct MapType {
  name: String
  flags: Bool
  members: Array[(String, Int)] // constant (TYPE_VALUE) and value
  description: String
} derive(Debug)

// A dump the map was written for, identified by the SHA-256 of its PRG
// and CHR ROM (without any iNES header, so headered and raw dumps agree).
pub(all) struct KnownRom {
  sha256: String
  prg: Int // bytes
  chr: Int
  label: String
} derive(Debug)

pub(all) struct MapBank {
  index: Int
  name: String
  description: String
} derive(Debug)

pub(all) struct RomMap {
  entries: Array[MapEntry]
  roms: Array[KnownRom]
  types: Array[MapType]
  banks: Array[MapBank]
} derive(Debug)

pub fn RomMap::empty() -> RomMap {
  { entries: [], roms: [], types: [], banks: [] }
}

// The name of a switchable bank, the prefix of its entries.
pub fn RomMap::bank_name(self: RomMap, bank: Int) -> String {
  match self.banks.iter().find_first(fn(b) { b.index == bank }) {
    Some(b) => b.name
    None => "bank\{bank}"
  }
}

// The location of a code address in a code table: the addresses of the
// pointers it holds.
pub fn MapEntry::code_addresses(self: MapEntry) -> Array[Int] {
  guard self.kind == Code else { return [] }
  let count = (self.length - 2) / self.stride + 1
  Array::makei(count, fn(i) { self.address + i * self.stride })
}

fn parse_number(text: String) -> Int raise ImageError {
  let digits = if text.has_prefix("$") { text.view(start_offset=1).to_owned() } else if text.has_prefix("0x") { text.view(start_offset=2).to_owned() } else { text }
  let radix = if text.has_prefix("$") || text.has_prefix("0x") { 16 } else { 10 }
  try @string.parse_int(digits, base=radix) catch { _ => raise InvalidImage("Invalid number in map: \{text}") }
}

// `$ADDR`, or `BANK:$ADDR` / `FIRST-LAST:$ADDR` for the switchable window:
// the address and its banks (empty when not qualified).
fn parse_location(text: String) -> (Int, Array[Int]) raise ImageError {
  guard text.find(":") is Some(i) else { return (parse_number(text), []) }
  let banks_text = text.view(end_offset=i).to_owned()
  let address = parse_number(text.view(start_offset=i + 1).to_owned())
  let (first, last) = match banks_text.find("-") {
    Some(j) => (parse_number(banks_text.view(end_offset=j).to_owned()), parse_number(banks_text.view(start_offset=j + 1).to_owned()))
    None => {
      let b = parse_number(banks_text)
      (b, b)
    }
  }
  if first < 0 || last < first || last > 255 { raise InvalidImage("Invalid banks in map: \{text}") }
  if address < 0x8000 || address >= 0xc000 { raise InvalidImage("Only addresses in the switchable window ($8000-$BFFF) have banks: \{text}") }
  (address, Array::makei(last - first + 1, fn(k) { first + k }))
}

pub fn parse_map(text: String) -> RomMap raise ImageError {
  let entries = []
  let roms = []
  let pending: Array[(MapKind, Int, String, Int, String, Int, Int, Array[Int])] = []
  let types: Array[MapType] = []
  let banks: Array[MapBank] = []
  let names: Set[String] = Set([])
  fn claim(name: String) raise ImageError {
    if names.contains(name) { raise InvalidImage("Duplicate map name: \{name}") }
    names.add(name)
  }
  for raw_line in text.split("\n") {
    let line = raw_line.to_owned()
    let (body, description) = match line.find(";") {
      Some(i) => (line.view(end_offset=i).to_owned(), line.view(start_offset=i + 1).trim().to_owned())
      None => (line, "")
    }
    let body = match body.find("#") { Some(i) => body.view(end_offset=i).to_owned(); None => body }
    let fields = body.split(" ").filter(fn(f) { !f.is_empty() }).map(fn(f) { f.trim().to_owned() }).to_array()
    if fields.is_empty() { continue }
    // rom SHA256 PRG_BYTES CHR_BYTES ; label
    if fields[0] == "rom" {
      if fields.length() != 4 || fields[1].length() != 64 { raise InvalidImage("Map rom line needs a SHA-256 and the PRG and CHR sizes: \{line}") }
      roms.push({ sha256: fields[1].to_lower(), prg: parse_number(fields[2]), chr: parse_number(fields[3]), label: description })
      continue
    }
    // enum NAME VALUE=N ... and flags NAME BIT=N ...
    if fields[0] == "enum" || fields[0] == "flags" {
      if fields.length() < 3 { raise InvalidImage("Map type needs a name and values: \{line}") }
      let name = fields[1]
      if types.iter().any(fn(t) { t.name == name }) { raise InvalidImage("Duplicate map type: \{name}") }
      let members = []
      for field in fields[2:] {
        guard field.find("=") is Some(i) else { raise InvalidImage("Map type value needs NAME=VALUE: \{field}") }
        let constant = name.to_upper() + "_" + field.view(end_offset=i).to_owned().to_upper()
        let value = parse_number(field.view(start_offset=i + 1).to_owned())
        if value < 0 || value > 255 || (fields[0] == "flags" && value == 0) {
          raise InvalidImage("Map type value out of range: \{field}")
        }
        claim(constant)
        members.push((constant, value))
      }
      types.push({ name, flags: fields[0] == "flags", members, description })
      continue
    }
    // bank N NAME ; description
    if fields[0] == "bank" {
      if fields.length() != 3 { raise InvalidImage("Map bank line needs a bank number and a name: \{line}") }
      let index = parse_number(fields[1])
      if banks.iter().any(fn(b) { b.index == index || b.name == fields[2] }) { raise InvalidImage("Duplicate map bank: \{line}") }
      banks.push({ index, name: fields[2], description })
      continue
    }
    if fields.length() < 3 { raise InvalidImage("Map line needs a kind, an address and a name: \{line}") }
    let kind = match fields[0] {
      "routine" => Routine
      "table" => Table
      "var" => Var
      "io" => Io
      "code" => Code
      other => raise InvalidImage("Unknown map entry kind: \{other}")
    }
    let (address, in_banks) = parse_location(fields[1])
    if !in_banks.is_empty() && (kind == Var || kind == Io) { raise InvalidImage("Only routines and tables are in banks: \{line}") }
    let name = if kind == Routine { fields[2] } else { fields[2].to_upper() }
    // [LENGTH] [: TYPE], or COUNT [STRIDE] [rts] for code tables
    let mut length = 0
    let mut stride = 0
    let mut adjust = 0
    let mut type_name = ""
    let mut i = 3
    while i < fields.length() {
      let field = fields[i]
      if field == ":" && i + 1 < fields.length() {
        type_name = fields[i + 1]
        i += 2
      } else if field.has_prefix(":") {
        type_name = field.view(start_offset=1).to_owned()
        i += 1
      } else if i == 3 && type_name == "" {
        length = parse_number(field)
        i += 1
      } else if field == "rts" && kind == Code && i >= 4 {
        adjust = 1
        i += 1
      } else if i == 4 && kind == Code {
        stride = parse_number(field)
        i += 1
      } else {
        raise InvalidImage("Unexpected map field: \{field}")
      }
    }
    if (kind == Table || kind == Code) && length <= 0 { raise InvalidImage("Table \{name} needs a length") }
    if type_name != "" && kind != Var { raise InvalidImage("Only variables have a type: \{line}") }
    if kind == Code {
      if stride == 0 { stride = 2 }
      if stride < 2 { raise InvalidImage("Code addresses are two bytes: \{line}") }
      length = (length - 1) * stride + 2 // count to bytes
    }
    // one entry per bank, and the window as fixed-bank code sees it
    if in_banks.is_empty() {
      entries.push({ kind, address, name, length, type_name, description, bank: -1, stride, adjust })
    } else {
      pending.push((kind, address, name, length, description, stride, adjust, in_banks))
    }
  }
  // Bank names can follow their entries: expand those in banks now.
  for item in pending {
    let (kind, address, name, length, description, stride, adjust, in_banks) = item
    for bank in in_banks {
      let prefix = { entries: [], roms: [], types: [], banks }.bank_name(bank)
      let prefix = if kind == Routine { prefix } else { prefix.to_upper() }
      entries.push({ kind, address, name: prefix + "_" + name, length, type_name: "", description, bank, stride, adjust })
    }
    // the same name at the same window address from several lines is one
    if !entries.iter().any(fn(e) { e.name == name && e.address == address && e.bank == -1 }) {
      entries.push({ kind, address, name, length, type_name: "", description, bank: -1, stride, adjust })
    }
  }
  for entry in entries { claim(entry.name) }
  for entry in entries {
    if entry.type_name != "" && !types.iter().any(fn(t) { t.name == entry.type_name }) {
      raise InvalidImage("Unknown map type: \{entry.type_name}")
    }
  }
  { entries, roms, types, banks }
}

pub(all) struct Cartridge {
  prg: Array[Int]
  chr: Array[Int] // empty: 8 KiB of CHR RAM
  mapper: Int // 0: NROM, 1: MMC1
} derive(Debug)

// MMC1 boards switch 16 KiB of PRG ROM at $8000 with the last bank fixed at
// $C000 (the mode games set at power-on); CHR is RAM, or 8 KiB of ROM.
fn check_mmc1(prg_size: Int, chr_size: Int) -> Unit raise ImageError {
  let banks = prg_size / 16384
  if prg_size % 16384 != 0 || banks < 2 || banks > 16 || (banks & (banks - 1)) != 0 {
    raise InvalidImage("MMC1 needs 32 to 256 KiB of PRG ROM in 16 KiB banks")
  }
  if chr_size != 0 && chr_size != 8192 { raise InvalidImage("Only MMC1 boards with CHR RAM or 8 KiB of CHR ROM are supported") }
}

pub(all) struct Dump {
  cartridge: Cartridge
  sha256: String // of PRG and CHR, without a header
  headered: Bool
} derive(Debug)

// A cartridge from an iNES file, or from a raw dump of the cartridge's ROMs
// (PRG then CHR, no header). A raw dump is split by the sizes of a dump the
// map knows of the same length, or as 16 or 32 KiB of PRG and 8 KiB of CHR;
// more than 32 KiB of PRG ROM is an MMC1 board.
pub fn load_dump(bytes: Bytes, map: RomMap) -> Dump raise ImageError {
  let headered = bytes.length() >= 4 && bytes[0] == b'N' && bytes[1] == b'E' && bytes[2] == b'S' && bytes[3] == b'\x1a'
  let cartridge = if headered {
    load_ines(bytes)
  } else {
    let size = bytes.length()
    let prg_size = match map.roms.iter().find_first(fn(r) { r.prg + r.chr == size }) {
      Some(known) => known.prg
      None =>
        if size == 0x6000 || size == 0xa000 {
          size - 0x2000
        } else {
          raise InvalidImage("Not an iNES file, nor a raw NROM dump (24 or 40 KiB): \{size} bytes")
        }
    }
    let mapper = if prg_size > 32768 { 1 } else { 0 }
    if mapper == 1 {
      check_mmc1(prg_size, size - prg_size)
    } else if prg_size != 16384 && prg_size != 32768 {
      raise InvalidImage("NROM needs 16 or 32 KiB of PRG ROM")
    }
    {
      prg: Array::makei(prg_size, fn(i) { bytes[i].to_int() }),
      chr: Array::makei(size - prg_size, fn(i) { bytes[prg_size + i].to_int() }),
      mapper,
    }
  }
  let contents = cartridge.prg.copy()
  contents.append(cartridge.chr)
  { cartridge, sha256: sha256(contents), headered }
}

// The dump the map describes, if this is one of them.
pub fn RomMap::identify(self: RomMap, dump: Dump) -> KnownRom? {
  self.roms.iter().find_first(fn(r) { r.sha256 == dump.sha256 })
}

// A warning when the map names known dumps and this is none of them.
pub fn RomMap::warning(self: RomMap, dump: Dump) -> String? {
  if self.roms.is_empty() || self.identify(dump) is Some(_) { return None }
  let expected = self.roms.map(fn(r) { if r.label == "" { r.sha256 } else { r.label } }).join(", ")
  Some("Unrecognised ROM (SHA-256 of PRG and CHR \{dump.sha256}). The map was written for \{expected}: names may not match and the game may not work.")
}

// iNES file with mapper 0 (NROM: 16 or 32 KiB of PRG ROM, 8 KiB of CHR ROM)
// or 1 (MMC1, see check_mmc1).
pub fn load_ines(bytes: Bytes) -> Cartridge raise ImageError {
  if bytes.length() < 16 || bytes[0] != b'N' || bytes[1] != b'E' || bytes[2] != b'S' || bytes[3] != b'\x1a' {
    raise InvalidImage("Not an iNES file")
  }
  let prg_size = bytes[4].to_int() * 16384
  let chr_size = bytes[5].to_int() * 8192
  // Old dumping tools left text in bytes 12-15, where byte 7 is not to be
  // trusted either.
  let archaic = (bytes[7].to_int() & 0x0c) != 8 && (bytes[12] != b'\x00' || bytes[13] != b'\x00' || bytes[14] != b'\x00' || bytes[15] != b'\x00')
  let mapper = (bytes[6].to_int() >> 4) | (if archaic { 0 } else { bytes[7].to_int() & 0xf0 })
  match mapper {
    0 => if prg_size != 16384 && prg_size != 32768 { raise InvalidImage("NROM needs 16 or 32 KiB of PRG ROM") }
    1 => check_mmc1(prg_size, chr_size)
    _ => raise InvalidImage("Only mappers 0 (NROM) and 1 (MMC1) are supported, not \{mapper}")
  }
  let start = 16 + (if (bytes[6].to_int() & 4) != 0 { 512 } else { 0 })
  if bytes.length() < start + prg_size + chr_size { raise InvalidImage("Truncated iNES file") }
  let prg = Array::makei(prg_size, fn(i) { bytes[start + i].to_int() })
  let chr = Array::makei(chr_size, fn(i) { bytes[start + prg_size + i].to_int() })
  { prg, chr, mapper }
}

fn byte_const(value: Int) -> @parse.SourceExpr {
  Const(base=Hexadecimal, value~, size=if value < 256 { Byte } else { Word })
}

// The image of a ROM before code discovery: map labels, tables as data
// directives (they bound inline jump tables), and variable definitions.
pub fn rom_image(cartridge: Cartridge, map: RomMap) -> Image raise ImageError {
  let banked = cartridge.mapper != 0
  let count = cartridge.prg.length() / 16384
  let origin = if banked || cartridge.prg.length() == 16384 { 0xc000 } else { 0x8000 }
  // with banks, the last is fixed at $C000 and any can be switched in
  let bytes = if banked { Array::makei(16384, fn(i) { cartridge.prg[(count - 1) * 16384 + i] }) } else { cartridge.prg.copy() }
  let banks : Banks = if banked {
    {
      contents: Array::makei(count, fn(k) { Array::makei(16384, fn(i) { cartridge.prg[k * 16384 + i] }) }),
      names: Array::makei(count, fn(k) { map.bank_name(k) }),
      windows: Map([]),
      window_names: Map([]),
      code: [],
    }
  } else { Banks::none() }
  let symbols: Map[String, Int] = Map([])
  let items: Array[(Int, @parse.SourceItem)] = []
  let named_data = []
  for entry in map.entries {
    if entry.bank >= 0 && (!banked || entry.bank >= count) {
      raise InvalidImage("\{entry.name} is in bank \{entry.bank}, which the cartridge does not have")
    }
    let location = if entry.bank >= 0 { bank_location(entry.bank, entry.address) } else { entry.address }
    // the window as fixed-bank code sees it holds no bytes of its own
    let window = banked && entry.bank < 0 && in_window(entry.address)
    match entry.kind {
      Var | Io => items.push((-1, Define(name=entry.name, rhs=byte_const(entry.address))))
      Routine => {
        if entry.bank >= 0 {
          if !banks.windows.contains(entry.address) { banks.windows[entry.address] = [] }
          if !banks.windows[entry.address].contains(entry.bank) { banks.windows[entry.address].push(entry.bank) }
        }
        symbols[entry.name] = location
        if window {
          // called from the fixed bank, it is code of the bank mapped then
          if !banks.window_names.contains(entry.address) { banks.window_names[entry.address] = entry.name }
        } else {
          items.push((location, Label(entry.name)))
        }
      }
      Table | Code if window => items.push((-1, Define(name=entry.name, rhs=byte_const(entry.address))))
      Table | Code => {
        let (start, size) = if entry.bank >= 0 { (0x8000, 16384) } else { (origin, bytes.length()) }
        if entry.address < start || entry.address + entry.length > start + size {
          raise InvalidImage("Table \{entry.name} is outside the PRG image")
        }
        let data = if entry.bank >= 0 { banks.contents[entry.bank] } else { bytes }
        symbols[entry.name] = location
        items.push((location, Label(entry.name)))
        let args = Array::makei(entry.length, fn(i) { byte_const(data[entry.address - start + i]) })
        items.push((location, Directive(name="db", args~)))
        named_data.push((location, entry.length))
        for address in entry.code_addresses() {
          banks.code.push((if entry.bank >= 0 { bank_location(entry.bank, address) } else { address }, entry.adjust))
        }
      }
    }
  }
  // Stable by address: definitions first, then labels before their data.
  let order = Array::makei(items.length(), fn(i) { i })
  order.sort_by(fn(i, j) { if items[i].0 != items[j].0 { items[i].0 - items[j].0 } else { i - j } })
  let source = order.map(fn(i) { items[i].1 })
  let addresses = order.map(fn(i) { if items[i].0 < 0 { 0 } else { items[i].0 } })
  { origin, bytes, symbols, addresses, source, banks, data: named_data }
}

priv struct Namer {
  vars: Array[MapEntry] // RAM and I/O, ascending
  tables: Array[MapEntry] // ascending
  banked: Bool
}

fn Namer::new(map: RomMap, banked~: Bool) -> Namer {
  let vars = map.entries.filter(fn(e) { e.kind == Var || e.kind == Io })
  vars.sort_by(fn(a, b) { a.address - b.address })
  let tables = map.entries.filter(fn(e) { e.kind == Table || e.kind == Code })
  tables.sort_by(fn(a, b) { a.address - b.address })
  { vars, tables, banked }
}

fn offset_expr(name: String, offset: Int) -> @parse.SourceExpr {
  let base = @parse.SourceExpr::Ident(name)
  if offset == 0 { base } else { Binary(op=Add, lhs=base, rhs=Const(base=Decimal, value=offset, size=Byte)) }
}

// A data address as `name` or `name + offset`. ROM addresses use the table
// containing them. Variables can share an address: an entry with a length is
// an array, one without is a scalar. Indexed accesses prefer an array that
// starts there, plain accesses a scalar; otherwise the nearest array that
// contains the address, then the variable it falls after. An indexed read
// just before a table is that table with the index starting at 1 or more.
// Code in a bank reads the window's tables of its bank (`bank`; -1 for the
// fixed bank, which reads the window as mapped).
fn Namer::name(self: Namer, address: Int, size: @parse.ConstantSize, indexed~: Bool, bank~: Int = -1) -> @parse.SourceExpr {
  let tables = self.tables.filter(fn(t) {
    t.bank == (if self.banked && in_window(address) { bank } else { -1 })
  })
  for t in tables {
    if address >= t.address && address < t.address + t.length { return offset_expr(t.name, address - t.address) }
  }
  if indexed && address >= 0x8000 {
    for t in tables {
      if t.address > address && t.address - address <= 8 {
        return Binary(op=Sub, lhs=Ident(t.name), rhs=Const(base=Decimal, value=t.address - address, size=Byte))
      }
    }
  }
  let exact_array = self.vars.iter().find_first(fn(v) { v.address == address && v.length > 0 })
  let exact_scalar = self.vars.iter().find_first(fn(v) { v.address == address && v.length == 0 })
  let (first, second) = if indexed { (exact_array, exact_scalar) } else { (exact_scalar, exact_array) }
  if first is Some(v) { return Ident(v.name) }
  if second is Some(v) { return Ident(v.name) }
  let mut containing: MapEntry? = None
  for v in self.vars {
    if v.length > 0 && v.address <= address && address < v.address + v.length {
      if containing is Some(c) && c.address >= v.address { continue }
      containing = Some(v)
    }
  }
  if containing is Some(v) { return offset_expr(v.name, address - v.address) }
  let mut before: MapEntry? = None
  for i, v in self.vars {
    if v.address > address { break }
    let next = if i + 1 < self.vars.length() { self.vars[i + 1].address } else { v.address + 1 }
    if address < next { before = Some(v) }
  }
  match before {
    Some(v) => offset_expr(v.name, address - v.address)
    None => Const(base=Hexadecimal, value=address, size~)
  }
}

fn rom_mode(image: Image, namer: Namer, inst: Instruction) -> @parse.SourceAddressingMode raise ImageError {
  let size: @parse.ConstantSize = if width(inst.mode) == 2 { Byte } else { Word }
  let bank = bank_of(inst.pc).unwrap_or(-1)
  // Lowering picks zero-page forms by value, so an absolute encoding of a
  // zero-page address would change indexed wrap-around.
  let absolute = inst.mode == "abs" || inst.mode == "absx" || inst.mode == "absy"
  if absolute && inst.arg < 256 && inst.op != JMP && inst.op != JSR {
    let zp = match inst.mode { "abs" => "zp"; "absx" => "zpx"; _ => "zpy" }
    if encoding(inst.op, zp) != None {
      raise InvalidImage("Absolute encoding of zero-page address at \{show_pc(inst.pc)} is not supported")
    }
  }
  match inst.mode {
    "imp" | "acc" | "imm" | "rel" => source_mode(image, inst)
    "zp" | "abs" =>
      if inst.op == JMP || inst.op == JSR { source_mode(image, inst) }
      else { AbsoluteOrZeroPage(addr=namer.name(inst.arg, size, indexed=false, bank~)) }
    "zpx" | "absx" => AbsoluteOrZeroPageX(addr=namer.name(inst.arg, size, indexed=true, bank~))
    "zpy" | "absy" => AbsoluteOrZeroPageY(addr=namer.name(inst.arg, size, indexed=true, bank~))
    // pointers: the variable holding the address
    "indx" => IndexedIndirect(addr=namer.name(inst.arg, size, indexed=true))
    "indy" => IndirectIndexed(addr=namer.name(inst.arg, size, indexed=false))
    "ind" => Indirect(addr=namer.name(inst.arg, size, indexed=false, bank~))
    _ => abort("Invalid decoded addressing mode")
  }
}

// Recompiles a ROM: discovers code from the vectors and builds the source
// view the lowering consumes, with map names for routines, tables and
// variables, and the map's descriptions as comments.
pub fn discover_rom(image: Image, map: RomMap) -> Program raise ImageError {
  let { instructions, dispatches, roots, facts, idle, windows, jumps, waits, traps, needed, following, pcs } = explore(image)
  // a transfer into data names its trap instead of the data
  let target_name = fn(pc: Int) { if traps.contains(pc) { trap_name(pc) } else { address_name(image, pc) } }
  let namer = Namer::new(map, banked=image.banked())
  let source = []
  let addresses = []
  fn append(item: @parse.SourceItem, pc: Int) {
    source.push(item)
    addresses.push(pc)
  }
  // the window as fixed-bank code sees it: names without bytes of their own
  let window = fn(entry: MapEntry) { image.banked() && entry.bank < 0 && in_window(entry.address) }
  for entry in map.entries {
    if entry.kind == Var || entry.kind == Io || ((entry.kind == Table || entry.kind == Code) && window(entry)) {
      append(Define(name=entry.name, rhs=byte_const(entry.address)), 0)
      if entry.description != "" { append(Comment(comment=entry.description, is_inline=true), 0) }
    }
  }
  let routines: Map[Int, Array[MapEntry]] = Map([])
  let tables: Map[Int, Array[MapEntry]] = Map([])
  for entry in map.entries {
    if window(entry) { continue }
    let target = match entry.kind { Routine => routines; Table | Code => tables; _ => continue }
    let location = if entry.bank >= 0 { bank_location(entry.bank, entry.address) } else { entry.address }
    if !target.contains(location) { target[location] = [] }
    target[location].push(entry)
  }
  for pc in needed {
    if instructions.contains(pc) {
      let name = address_name(image, pc)
      if !image.symbols.contains(name) { image.symbols[name] = pc }
    }
  }
  let events: Set[Int] = Set(pcs)
  for address, _ in routines { events.add(address) }
  for address, _ in tables { events.add(address) }
  let order = events.to_array()
  order.sort()
  for pc in order {
    if routines.get(pc) is Some(entries) {
      // the description follows the label, documenting the routine
      for entry in entries {
        append(Label(entry.name), pc)
        if entry.description != "" { append(Comment(comment=entry.description, is_inline=false), pc) }
      }
    }
    if tables.get(pc) is Some(entries) {
      for entry in entries {
        if entry.description != "" { append(Comment(comment=entry.description, is_inline=false), pc) }
        append(Label(entry.name), pc)
        let args = []
        for i in 0..<entry.length { args.push(byte_const(image.read(rebase(pc, (pc & 0xffff) + i)))) }
        append(Directive(name="db", args~), pc)
      }
    }
    guard instructions.get(pc) is Some(inst) else { continue }
    let name = address_name(image, pc)
    if !routines.contains(pc) && name.has_prefix("loc_") && needed.contains(pc) { append(Label(name), pc) }
    let branch = if inst.mode == "rel" { condition(inst.op, facts[pc]) } else { None }
    if branch == Some(true) {
      append(Inst(op=JMP, mode=AbsoluteOrZeroPage(addr=Ident(target_name(branch_target(inst))))), pc)
    } else if inst.op == JMP && inst.mode == "ind" && rom_vector(image, inst) is Some(target) {
      append(Inst(op=JMP, mode=AbsoluteOrZeroPage(addr=Ident(target_name(target)))), pc)
    } else if inst.mode == "rel" && branch != Some(false) {
      append(Inst(op=inst.op, mode=Relative(addr=Ident(target_name(branch_target(inst))))), pc)
    } else if (inst.op == JMP || inst.op == JSR) && inst.mode == "abs" {
      append(Inst(op=inst.op, mode=AbsoluteOrZeroPage(addr=Ident(target_name(jump_target(inst))))), pc)
    } else if branch != Some(false) {
      append(Inst(op=inst.op, mode=rom_mode(image, namer, inst)), pc)
    }
    let falls = inst.op != JMP && inst.op != RTS && inst.op != RTI &&
      inst.op != BRK && !dispatches.contains(pc) && branch != Some(true)
    if inst.op == JSR && dispatches.get(pc) is None && always_unwinds(image, jump_target(inst)) {
      // the callee returns past this routine: nothing follows the call
      append(Inst(op=JMP, mode=AbsoluteOrZeroPage(addr=Ident(trap_name(inst.next)))), pc)
    } else if falls && (following.get(pc) != Some(inst.next) || traps.contains(inst.next)) {
      append(Inst(op=JMP, mode=AbsoluteOrZeroPage(addr=Ident(target_name(inst.next)))), pc)
    }
  }
  // The emitted data file reads labels and comments from the image source.
  image.source.clear()
  image.addresses.clear()
  for i, item in source {
    image.source.push(item)
    image.addresses.push(addresses[i])
  }
  { image, instructions, dispatches, roots: roots.map(fn(pc) { address_name(image, pc) }), facts, idle, windows, jumps, waits, exact: Ref(false), source, addresses, map }
}
