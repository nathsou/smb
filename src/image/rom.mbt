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
} derive(Debug, Eq)

pub(all) struct MapEntry {
  kind: MapKind
  address: Int
  name: String
  length: Int // bytes; 0 when not given
  type_name: String // a type of the map for its bytes; "" when not given
  description: String
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

pub(all) struct RomMap {
  entries: Array[MapEntry]
  roms: Array[KnownRom]
  types: Array[MapType]
} derive(Debug)

pub fn RomMap::empty() -> RomMap {
  { entries: [], roms: [], types: [] }
}

fn parse_number(text: String) -> Int raise ImageError {
  let digits = if text.has_prefix("$") { text.view(start_offset=1).to_owned() } else if text.has_prefix("0x") { text.view(start_offset=2).to_owned() } else { text }
  let radix = if text.has_prefix("$") || text.has_prefix("0x") { 16 } else { 10 }
  try @string.parse_int(digits, base=radix) catch { _ => raise InvalidImage("Invalid number in map: \{text}") }
}

pub fn parse_map(text: String) -> RomMap raise ImageError {
  let entries = []
  let roms = []
  let types: Array[MapType] = []
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
    if fields.length() < 3 { raise InvalidImage("Map line needs a kind, an address and a name: \{line}") }
    let kind = match fields[0] {
      "routine" => Routine
      "table" => Table
      "var" => Var
      "io" => Io
      other => raise InvalidImage("Unknown map entry kind: \{other}")
    }
    let address = parse_number(fields[1])
    let name = if kind == Routine { fields[2] } else { fields[2].to_upper() }
    claim(name)
    // [LENGTH] [: TYPE]
    let mut length = 0
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
      } else {
        raise InvalidImage("Unexpected map field: \{field}")
      }
    }
    if kind == Table && length <= 0 { raise InvalidImage("Table \{name} needs a length") }
    if type_name != "" && kind != Var { raise InvalidImage("Only variables have a type: \{line}") }
    entries.push({ kind, address, name, length, type_name, description })
  }
  for entry in entries {
    if entry.type_name != "" && !types.iter().any(fn(t) { t.name == entry.type_name }) {
      raise InvalidImage("Unknown map type: \{entry.type_name}")
    }
  }
  { entries, roms, types }
}

pub(all) struct Cartridge {
  prg: Array[Int]
  chr: Array[Int]
} derive(Debug)

pub(all) struct Dump {
  cartridge: Cartridge
  sha256: String // of PRG and CHR, without a header
  headered: Bool
} derive(Debug)

// A cartridge from an iNES file, or from a raw dump of the cartridge's ROMs
// (PRG then CHR, no header). A raw dump is split by the sizes of a dump the
// map knows of the same length, or as 16 or 32 KiB of PRG and 8 KiB of CHR.
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
    if prg_size != 16384 && prg_size != 32768 { raise InvalidImage("NROM needs 16 or 32 KiB of PRG ROM") }
    {
      prg: Array::makei(prg_size, fn(i) { bytes[i].to_int() }),
      chr: Array::makei(size - prg_size, fn(i) { bytes[prg_size + i].to_int() }),
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

// iNES file with mapper 0 (NROM): 16 or 32 KiB of PRG ROM, 8 KiB of CHR ROM.
pub fn load_ines(bytes: Bytes) -> Cartridge raise ImageError {
  if bytes.length() < 16 || bytes[0] != b'N' || bytes[1] != b'E' || bytes[2] != b'S' || bytes[3] != b'\x1a' {
    raise InvalidImage("Not an iNES file")
  }
  let prg_size = bytes[4].to_int() * 16384
  let chr_size = bytes[5].to_int() * 8192
  let mapper = (bytes[6].to_int() >> 4) | (bytes[7].to_int() & 0xf0)
  if mapper != 0 { raise InvalidImage("Only mapper 0 (NROM) is supported, not \{mapper}") }
  if prg_size != 16384 && prg_size != 32768 { raise InvalidImage("NROM needs 16 or 32 KiB of PRG ROM") }
  let start = 16 + (if (bytes[6].to_int() & 4) != 0 { 512 } else { 0 })
  if bytes.length() < start + prg_size + chr_size { raise InvalidImage("Truncated iNES file") }
  let prg = Array::makei(prg_size, fn(i) { bytes[start + i].to_int() })
  let chr = Array::makei(chr_size, fn(i) { bytes[start + prg_size + i].to_int() })
  { prg, chr }
}

fn byte_const(value: Int) -> @parse.SourceExpr {
  Const(base=Hexadecimal, value~, size=if value < 256 { Byte } else { Word })
}

// The image of a ROM before code discovery: map labels, tables as data
// directives (they bound inline jump tables), and variable definitions.
pub fn rom_image(cartridge: Cartridge, map: RomMap) -> Image raise ImageError {
  let origin = if cartridge.prg.length() == 16384 { 0xc000 } else { 0x8000 }
  let symbols: Map[String, Int] = Map([])
  let items: Array[(Int, @parse.SourceItem)] = []
  for entry in map.entries {
    match entry.kind {
      Var | Io => items.push((-1, Define(name=entry.name, rhs=byte_const(entry.address))))
      Routine => {
        symbols[entry.name] = entry.address
        items.push((entry.address, Label(entry.name)))
      }
      Table => {
        if entry.address < origin || entry.address + entry.length > origin + cartridge.prg.length() {
          raise InvalidImage("Table \{entry.name} is outside the PRG image")
        }
        symbols[entry.name] = entry.address
        items.push((entry.address, Label(entry.name)))
        let args = Array::makei(entry.length, fn(i) { byte_const(cartridge.prg[entry.address - origin + i]) })
        items.push((entry.address, Directive(name="db", args~)))
      }
    }
  }
  // Stable by address: definitions first, then labels before their data.
  let order = Array::makei(items.length(), fn(i) { i })
  order.sort_by(fn(i, j) { if items[i].0 != items[j].0 { items[i].0 - items[j].0 } else { i - j } })
  let source = order.map(fn(i) { items[i].1 })
  let addresses = order.map(fn(i) { if items[i].0 < 0 { 0 } else { items[i].0 } })
  { origin, bytes: cartridge.prg.copy(), symbols, addresses, source }
}

priv struct Namer {
  vars: Array[MapEntry] // RAM and I/O, ascending
  tables: Array[MapEntry] // ascending
}

fn Namer::new(map: RomMap) -> Namer {
  let vars = map.entries.filter(fn(e) { e.kind == Var || e.kind == Io })
  vars.sort_by(fn(a, b) { a.address - b.address })
  let tables = map.entries.filter(fn(e) { e.kind == Table })
  tables.sort_by(fn(a, b) { a.address - b.address })
  { vars, tables }
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
fn Namer::name(self: Namer, address: Int, size: @parse.ConstantSize, indexed~: Bool) -> @parse.SourceExpr {
  for t in self.tables {
    if address >= t.address && address < t.address + t.length { return offset_expr(t.name, address - t.address) }
  }
  if indexed && address >= 0x8000 {
    for t in self.tables {
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
      else { AbsoluteOrZeroPage(addr=namer.name(inst.arg, size, indexed=false)) }
    "zpx" | "absx" => AbsoluteOrZeroPageX(addr=namer.name(inst.arg, size, indexed=true))
    "zpy" | "absy" => AbsoluteOrZeroPageY(addr=namer.name(inst.arg, size, indexed=true))
    // pointers: the variable holding the address
    "indx" => IndexedIndirect(addr=namer.name(inst.arg, size, indexed=true))
    "indy" => IndirectIndexed(addr=namer.name(inst.arg, size, indexed=false))
    "ind" => Indirect(addr=namer.name(inst.arg, size, indexed=false))
    _ => abort("Invalid decoded addressing mode")
  }
}

// Recompiles a ROM: discovers code from the vectors and builds the source
// view the lowering consumes, with map names for routines, tables and
// variables, and the map's descriptions as comments.
pub fn discover_rom(image: Image, map: RomMap) -> Program raise ImageError {
  let { instructions, dispatches, roots, facts, idle, needed, following, pcs } = explore(image)
  let namer = Namer::new(map)
  let source = []
  let addresses = []
  fn append(item: @parse.SourceItem, pc: Int) {
    source.push(item)
    addresses.push(pc)
  }
  for entry in map.entries {
    if entry.kind == Var || entry.kind == Io {
      append(Define(name=entry.name, rhs=byte_const(entry.address)), 0)
      if entry.description != "" { append(Comment(comment=entry.description, is_inline=true), 0) }
    }
  }
  let routines: Map[Int, Array[MapEntry]] = Map([])
  let tables: Map[Int, Array[MapEntry]] = Map([])
  for entry in map.entries {
    let target = match entry.kind { Routine => routines; Table => tables; _ => continue }
    if !target.contains(entry.address) { target[entry.address] = [] }
    target[entry.address].push(entry)
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
        let args = Array::makei(entry.length, fn(i) { byte_const(image.bytes[pc - image.origin + i]) })
        append(Directive(name="db", args~), pc)
      }
    }
    guard instructions.get(pc) is Some(inst) else { continue }
    let name = address_name(image, pc)
    if !routines.contains(pc) && name.has_prefix("loc_") && needed.contains(pc) { append(Label(name), pc) }
    let branch = if inst.mode == "rel" { condition(inst.op, facts[pc]) } else { None }
    if branch == Some(true) {
      append(Inst(op=JMP, mode=AbsoluteOrZeroPage(addr=Ident(address_name(image, branch_target(inst))))), pc)
    } else if inst.op == JMP && inst.mode == "ind" {
      let hi = (inst.arg & 0xff00) | ((inst.arg + 1) & 255)
      let target = image.read(inst.arg) | (image.read(hi) << 8)
      append(Inst(op=JMP, mode=AbsoluteOrZeroPage(addr=Ident(address_name(image, target)))), pc)
    } else if branch != Some(false) {
      append(Inst(op=inst.op, mode=rom_mode(image, namer, inst)), pc)
    }
    let falls = inst.op != JMP && inst.op != RTS && inst.op != RTI &&
      inst.op != BRK && !dispatches.contains(pc) && branch != Some(true)
    if falls && following.get(pc) != Some(inst.next) {
      append(Inst(op=JMP, mode=AbsoluteOrZeroPage(addr=Ident(address_name(image, inst.next)))), pc)
    }
  }
  // The emitted data file reads labels and comments from the image source.
  image.source.clear()
  image.addresses.clear()
  for i, item in source {
    image.source.push(item)
    image.addresses.push(addresses[i])
  }
  { image, instructions, dispatches, roots: roots.map(fn(pc) { address_name(image, pc) }), facts, idle, source, addresses, map }
}
