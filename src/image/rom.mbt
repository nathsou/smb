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
// Cartridges with a mapper switch banks of PRG ROM into windows while
// others stay fixed (see `Layout`): MMC1 a 16 KiB bank at $8000-$BFFF, the
// last bank staying at $C000-$FFFF; MMC3 8 KiB banks into two windows, the
// last two banks fixed (`prg_mode 1` in the map for games that set PRG mode
// 1, fixing them at $8000 and $E000). The map names the banks and qualifies
// addresses in the windows with the banks they are in:
//
//   bank    1 brinstar                   (a name for bank 1's entries)
//   bank    0 objects $c000              (and the window it was made for)
//   bank   13 tiles $a000 with 14         (whose code runs with bank 14 in the
//                                         other window)
//   routine 1-5:$80b0 update_objects     (in banks 1 to 5)
//   table   0:$9560 rooms 40             (in bank 0)
//   code    $c51f area_inits 6           (6 code addresses, a word each)
//   code    0-5:$b290 sound_defaults 9 5 (9 code addresses, 5 bytes apart)
//   code    1-5:$8048 moves 8 2 rts      (addresses minus one, for RTS)
//
// Games keep the bank they want in each window in RAM and switch from
// there; where discovery does not otherwise know the bank mapped in a
// window, it is the one in its shadow (which the interrupt handlers keep):
//
//   shadow  $a000 $0720                  (the bank of the window at $A000)
//
// For MMC1 it follows the bank the five serial writes to the PRG register
// switch in when the map asks (`lda #3 / jsr switch / jsr $8000` calls bank
// 3's routine):
//
//   banks   follow
//
// The interrupt runs where the foreground waits for it. A game that computes
// for frames on end in the foreground (a chess engine thinking) also yields
// to it: after a number of transfers back (taken branches and jumps to an
// address at or before them) in a frame, at the next one, outside the code
// the interrupt handlers run:
//
//   preempt 500
//
// Calls are C calls, and leave no return address on the guest stack, unless
// the program reads them (see the stack analysis) or the map asks for the
// machine's stack, byte for byte (a game compared with the interpreter):
//
//   exact
//
// A path the game only takes when something went wrong (its RAM lost, say)
// can be cut where it starts, which then traps:
//
//   trap    $f7a3                        ; optional description
//
// A computed jump goes to the addresses of code tables (`code`), or where
// the map says besides (a default the game sets with immediates):
//
//   jump    $e1cb $e091                  (the jump at $E1CB can go to $E091)
//
// A routine can read bytes that follow its calls, through its return
// address, and return past them (its calls then keep return addresses on
// the guest stack, see `exact`):
//
//   inline  $aa98 2                      (two bytes after each call)
//
// Code the program writes to RAM and runs there is recompiled from the
// bytes the map gives (the generated code traps if RAM holds others):
//
//   ram     $07fd 6c 00 00               (jmp ($0000), where the NMI goes)
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
  Trap // where execution traps instead of continuing
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
  window: Int // the window the bank was made for, -1 when not given
  companion: Int // the bank in the other window whenever this one runs, -1 when not given
  description: String
} derive(Debug)

pub(all) struct RomMap {
  entries: Array[MapEntry]
  roms: Array[KnownRom]
  types: Array[MapType]
  banks: Array[MapBank]
  prg_mode: Int // MMC3's PRG mode the game sets (bit 6 of its bank select)
  shadows: Array[(Int, Int)] // window address, RAM address
  exact: Bool // calls keep return addresses on the guest stack (see `exact`)
  follow: Bool // discovery follows MMC1's bank (see `banks follow`)
  preempt: Int // transfers back in a frame after which the foreground yields (0: never, see `preempt`)
  jumps: Array[(Int, Int)] // a computed jump's location, and where it can go besides the code tables
  inline: Array[(Int, Int)] // a routine's location, and the bytes after its calls that it returns past
  ram_code: Array[(Int, Array[Int])] // code the program writes to RAM: address and bytes
} derive(Debug)

pub fn RomMap::empty() -> RomMap {
  { entries: [], roms: [], types: [], banks: [], prg_mode: 0, shadows: [], exact: false, follow: false, preempt: 0, jumps: [], inline: [], ram_code: [] }
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

// `$ADDR`, or `BANK:$ADDR` / `FIRST-LAST:$ADDR` for a switchable window:
// the address and its banks (empty when not qualified). That the address is
// in a window is checked against the cartridge (`rom_image`).
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
  (address, Array::makei(last - first + 1, fn(k) { first + k }))
}

// Names that cannot be C functions or constants.
let c_keywords : Array[String] = [
  "auto", "break", "case", "char", "const", "continue", "default", "do", "double", "else", "enum",
  "extern", "float", "for", "goto", "if", "inline", "int", "long", "register", "restrict", "return",
  "short", "signed", "sizeof", "static", "struct", "switch", "typedef", "union", "unsigned", "void",
  "volatile", "while", "main",
]

pub fn parse_map(text: String) -> RomMap raise ImageError {
  let entries = []
  let roms = []
  let pending: Array[(MapKind, Int, String, Int, String, Int, Int, Array[Int])] = []
  let types: Array[MapType] = []
  let banks: Array[MapBank] = []
  let mut prg_mode = 0
  let mut exact = false
  let mut follow = false
  let mut preempt = 0
  let jumps: Array[(Int, Int)] = []
  let inline: Array[(Int, Int)] = []
  let ram_code: Array[(Int, Array[Int])] = []
  let shadows: Array[(Int, Int)] = []
  let names: Set[String] = Set([])
  fn claim(name: String) raise ImageError {
    if names.contains(name) { raise InvalidImage("Duplicate map name: \{name}") }
    if c_keywords.contains(name) { raise InvalidImage("A map name cannot be a C keyword: \{name}") }
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
    // jump [BANK:]$SITE [BANK:]$TARGET: a computed jump can go there too
    if fields[0] == "jump" {
      if fields.length() != 3 { raise InvalidImage("Map jump line needs the jump's address and a target: \{line}") }
      let one = fn(text: String) raise ImageError {
        match parse_location(text) {
          (address, []) => address
          (address, [bank]) => bank_location(bank, address)
          _ => raise InvalidImage("A jump and its target are in one bank each: \{line}")
        }
      }
      jumps.push((one(fields[1]), one(fields[2])))
      continue
    }
    // inline [BANK:]$ROUTINE BYTES: its calls are followed by that many bytes
    if fields[0] == "inline" {
      if fields.length() != 3 { raise InvalidImage("Map inline line needs a routine and a number of bytes: \{line}") }
      let location = match parse_location(fields[1]) {
        (address, []) => address
        (address, [bank]) => bank_location(bank, address)
        _ => raise InvalidImage("An inline routine is in one bank: \{line}")
      }
      inline.push((location, parse_number(fields[2])))
      continue
    }
    // ram $ADDR BYTE...: code the program writes to RAM and runs there
    if fields[0] == "ram" {
      if fields.length() < 3 { raise InvalidImage("Map ram line needs an address and the code's bytes: \{line}") }
      let address = parse_number(fields[1])
      let bytes = []
      for f in fields[2:] { bytes.push(parse_number("$" + f)) }
      if address < 0 || address + bytes.length() > 0x800 || bytes.iter().any(fn(b) { b < 0 || b > 255 }) {
        raise InvalidImage("Code in RAM is in $0000-$07FF, a byte at a time in hexadecimal: \{line}")
      }
      ram_code.push((address, bytes))
      continue
    }
    // banks follow: discovery follows what MMC1's serial writes switch in
    if fields[0] == "banks" {
      if fields.length() != 2 || fields[1] != "follow" { raise InvalidImage("Map banks line: banks follow") }
      follow = true
      continue
    }
    // preempt N: the foreground yields after N transfers back in a frame
    if fields[0] == "preempt" {
      if fields.length() != 2 { raise InvalidImage("Map preempt line needs a number of transfers back: \{line}") }
      preempt = parse_number(fields[1])
      if preempt <= 0 || preempt > 65535 { raise InvalidImage("Map preempt is 1 to 65535: \{line}") }
      continue
    }
    // exact: calls keep their return addresses on the guest stack
    if fields[0] == "exact" {
      if fields.length() != 1 { raise InvalidImage("Map exact line takes nothing more: \{line}") }
      exact = true
      continue
    }
    // prg_mode N: the MMC3 PRG mode the game sets
    if fields[0] == "prg_mode" {
      if fields.length() != 2 || (fields[1] != "0" && fields[1] != "1") { raise InvalidImage("Map prg_mode line needs 0 or 1: \{line}") }
      prg_mode = parse_number(fields[1])
      continue
    }
    // shadow $WINDOW $RAM: the RAM byte with the bank of a window
    if fields[0] == "shadow" {
      if fields.length() != 3 { raise InvalidImage("Map shadow line needs a window and a RAM address: \{line}") }
      let ram = parse_number(fields[2])
      if ram >= 0x800 || (ram >= 0x100 && ram < 0x200) { raise InvalidImage("A shadow is internal RAM outside the stack page: \{line}") }
      shadows.push((parse_number(fields[1]), ram))
      continue
    }
    // bank N NAME [$WINDOW] ; description
    if fields[0] == "bank" {
      // bank N name [$window [with M]]
      let paired = fields.length() == 6 && fields[4] == "with"
      if fields.length() != 3 && fields.length() != 4 && !paired { raise InvalidImage("Map bank line needs a bank number and a name: \{line}") }
      let index = parse_number(fields[1])
      if banks.iter().any(fn(b) { b.index == index || b.name == fields[2] }) { raise InvalidImage("Duplicate map bank: \{line}") }
      let window = if fields.length() >= 4 { parse_number(fields[3]) } else { -1 }
      let companion = if paired { parse_number(fields[5]) } else { -1 }
      banks.push({ index, name: fields[2], window, companion, description })
      continue
    }
    // trap [BANK:]$ADDR ; description
    if fields[0] == "trap" {
      if fields.length() != 2 { raise InvalidImage("Map trap line needs an address: \{line}") }
      let (address, in_banks) = parse_location(fields[1])
      let name = "__trap__" + fields[1]
      if in_banks.is_empty() {
        entries.push({ kind: Trap, address, name, length: 1, type_name: "", description, bank: -1, stride: 0, adjust: 0 })
      } else {
        for bank in in_banks {
          entries.push({ kind: Trap, address, name: name + "_" + bank.to_string(), length: 1, type_name: "", description, bank, stride: 0, adjust: 0 })
        }
      }
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
      let prefix = { ..RomMap::empty(), banks }.bank_name(bank)
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
  { entries, roms, types, banks, prg_mode, shadows, exact: exact || !inline.is_empty(), follow, preempt, jumps, inline, ram_code }
}

pub(all) struct Cartridge {
  prg: Array[Int]
  chr: Array[Int] // empty: 8 KiB of CHR RAM
  mapper: Int // 0: NROM, 1: MMC1, 4: MMC3
} derive(Debug)

// MMC1 boards switch 16 KiB of PRG ROM at $8000 with the last bank fixed at
// $C000 (the mode games set at power-on); CHR is RAM, or up to 128 KiB of
// ROM in 4 KiB banks.
fn check_mmc1(prg_size: Int, chr_size: Int) -> Unit raise ImageError {
  let banks = prg_size / 16384
  if prg_size % 16384 != 0 || banks < 2 || banks > 16 || (banks & (banks - 1)) != 0 {
    raise InvalidImage("MMC1 needs 32 to 256 KiB of PRG ROM in 16 KiB banks")
  }
  if chr_size > 131072 || chr_size % 8192 != 0 { raise InvalidImage("MMC1 has CHR RAM or up to 128 KiB of CHR ROM") }
}

// MMC3 boards switch 8 KiB banks of PRG ROM and 1 or 2 KiB banks of CHR ROM
// (or have CHR RAM).
fn check_mmc3(prg_size: Int, chr_size: Int) -> Unit raise ImageError {
  let banks = prg_size / 8192
  if prg_size % 8192 != 0 || banks < 4 || banks > 64 || (banks & (banks - 1)) != 0 {
    raise InvalidImage("MMC3 needs 32 to 512 KiB of PRG ROM in 8 KiB banks")
  }
  if chr_size > 262144 || chr_size % 1024 != 0 { raise InvalidImage("MMC3 has up to 256 KiB of CHR ROM in 1 KiB banks") }
}

pub(all) struct Dump {
  cartridge: Cartridge
  sha256: String // of PRG and CHR, without a header
  headered: Bool
} derive(Debug)

// A cartridge from an iNES file, or from a raw dump of the cartridge's ROMs
// (PRG then CHR, no header). A raw dump is split by the sizes of a dump the
// map knows of the same length, or as 16 or 32 KiB of PRG and 8 KiB of CHR;
// more than 32 KiB of PRG ROM is an MMC1 board, or MMC3 with more than 8 KiB
// of CHR ROM.
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
    let mapper = if prg_size <= 32768 { 0 } else if size - prg_size > 8192 { 4 } else { 1 }
    if mapper == 1 {
      check_mmc1(prg_size, size - prg_size)
    } else if mapper == 4 {
      check_mmc3(prg_size, size - prg_size)
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

// iNES file with mapper 0 (NROM: 16 or 32 KiB of PRG ROM, 8 KiB of CHR ROM),
// 1 (MMC1, see check_mmc1) or 4 (MMC3, see check_mmc3).
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
    4 => check_mmc3(prg_size, chr_size)
    _ => raise InvalidImage("Only mappers 0 (NROM), 1 (MMC1) and 4 (MMC3) are supported, not \{mapper}")
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
  // (MMC1 with 32 KiB of PRG ROM switches nothing: its window holds the
  // first bank, the last fixed; its registers still bank the CHR)
  let banked = cartridge.mapper != 0 && !(cartridge.mapper == 1 && cartridge.prg.length() == 0x8000)
  let layout = match cartridge.mapper {
    _ if !banked => Layout::none()
    1 => Layout::mmc1(cartridge.prg.length() / 0x4000)
    4 => Layout::mmc3(cartridge.prg.length() / 0x2000, map.prg_mode)
    _ => Layout::none()
  }
  set_layout(layout)
  follow_mmc1(map.follow)
  let size = layout.bank_size
  let count = if banked { cartridge.prg.length() / size } else { 0 }
  // with banks, the fixed ones from the lowest to $FFFF (the windows between
  // them hold no bytes of their own) and any can be switched in
  let origin = if !banked {
    if cartridge.prg.length() == 16384 { 0xc000 } else { 0x8000 }
  } else {
    layout.fixed.fold(init=0x10000, fn(lowest, region) { if region.0 < lowest { region.0 } else { lowest } })
  }
  let bytes = if banked {
    let bytes = Array::make(0x10000 - origin, 0)
    for region in layout.fixed {
      for i in 0..<size { bytes[region.0 - origin + i] = cartridge.prg[region.1 * size + i] }
    }
    bytes
  } else { cartridge.prg.copy() }
  let banks : Banks = if banked {
    {
      contents: Array::makei(count, fn(k) { Array::makei(size, fn(i) { cartridge.prg[k * size + i] }) }),
      homes: Array::make(count, -1),
      companions: Array::make(count, -1),
      names: Array::makei(count, fn(k) { map.bank_name(k) }),
      windows: Map([]),
      window_names: Map([]),
      code: [],
      shadows: [],
      jumps: [],
      inline: [],
      ram_code: Map([]),
    }
  } else { Banks::none() }
  for jump in map.jumps { banks.jumps.push(jump) }
  for routine in map.inline { banks.inline.push(routine) }
  for code in map.ram_code {
    for i, byte in code.1 { banks.ram_code[code.0 + i] = byte }
  }
  for shadow in map.shadows {
    let window = window_of(shadow.0)
    if window < 0 || layout.windows[window] != shadow.0 { raise InvalidImage("Shadow of \{show_pc(shadow.0)}, which is not a window") }
    banks.shadows.push((window, shadow.1))
  }
  // each bank's window: as the map says, and from the addresses it gives
  let homes: Map[Int, Int] = Map([])
  for b in map.banks {
    if b.window < 0 { continue }
    if !banked || b.index >= count { raise InvalidImage("Map bank \{b.index}, which the cartridge does not have") }
    let window = window_of(b.window)
    if window < 0 || layout.windows[window] != b.window { raise InvalidImage("Bank \{b.index} is for \{show_pc(b.window)}, which is not a window") }
    homes[b.index] = window
    if b.companion >= 0 {
      if b.companion >= count || layout.windows.length() != 2 { raise InvalidImage("Bank \{b.index} runs with bank \{b.companion}, which needs two windows and a bank the cartridge has") }
      banks.companions[b.index] = b.companion
    }
  }
  for entry in map.entries {
    if entry.bank < 0 { continue }
    if !banked || entry.bank >= count {
      raise InvalidImage("\{entry.name} is in bank \{entry.bank}, which the cartridge does not have")
    }
    let window = window_of(entry.address)
    if window < 0 {
      raise InvalidImage("\{entry.name}: only addresses in switchable windows have banks (\{layout.windows.map(show_pc).join(", ")}, \{size / 1024} KiB each)")
    }
    match homes.get(entry.bank) {
      Some(other) if other != window =>
        raise InvalidImage("\{entry.name}: bank \{entry.bank} is named at \{show_pc(layout.windows[other])} and at \{show_pc(layout.windows[window])}")
      _ => homes[entry.bank] = window
    }
  }
  for bank, window in homes { banks.homes[bank] = window }
  let symbols: Map[String, Int] = Map([])
  let items: Array[(Int, @parse.SourceItem)] = []
  let named_data = []
  for entry in map.entries {
    let location = if entry.bank >= 0 { bank_location(entry.bank, entry.address) } else { entry.address }
    // the window as fixed-bank code sees it holds no bytes of its own
    let window = banked && entry.bank < 0 && in_window(entry.address)
    match entry.kind {
      Var | Io => items.push((-1, Define(name=entry.name, rhs=byte_const(entry.address))))
      // traps like data: execution never decodes it
      Trap => named_data.push((location, 1))
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
        let (start, extent) = if entry.bank >= 0 { (layout.windows[window_of(entry.address)], size) } else { (origin, bytes.length()) }
        if entry.address < start || entry.address + entry.length > start + extent ||
          (banked && entry.bank < 0 && (fixed_bank(entry.address) < 0 || fixed_bank(entry.address + entry.length - 1) < 0)) {
          raise InvalidImage("Table \{entry.name} is outside the PRG image")
        }
        let data = if entry.bank >= 0 { banks.contents[entry.bank] } else { bytes }
        symbols[entry.name] = location
        items.push((location, Label(entry.name)))
        let args = Array::makei(entry.length, fn(i) { byte_const(data[entry.address - start + i]) })
        items.push((location, Directive(name="db", args~)))
        named_data.push((location, entry.length))
        for address in entry.code_addresses() {
          banks.code.push((if entry.bank >= 0 { bank_location(entry.bank, address) } else { address }, entry.adjust, entry.address))
          // a code address of a bank in its window: the bank has a routine
          // there, which code outside the window reaches as mapped
          if entry.bank >= 0 {
            let target = (data[address - start] | (data[address - start + 1] << 8)) + entry.adjust
            if window_of(target) == window_of(entry.address) {
              if !banks.windows.contains(target) { banks.windows[target] = [] }
              if !banks.windows[target].contains(entry.bank) { banks.windows[target].push(entry.bank) }
            }
          }
        }
      }
    }
  }
  // Stable by address: definitions first, then labels before their data.
  let order = Array::makei(items.length(), fn(i) { i })
  order.sort_by(fn(i, j) { if items[i].0 != items[j].0 { items[i].0 - items[j].0 } else { i - j } })
  let source = order.map(fn(i) { items[i].1 })
  let addresses = order.map(fn(i) { if items[i].0 < 0 { 0 } else { items[i].0 } })
  Image::new(origin, bytes, symbols, addresses, source, banks, named_data)
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
// Code in a bank reads the tables of its bank in its window (`at` is the
// reading instruction); other code reads a window as mapped.
fn Namer::name(self: Namer, address: Int, size: @parse.ConstantSize, indexed~: Bool, at~: Int = 0) -> @parse.SourceExpr {
  let bank = bank_of(rebase(at, address)).unwrap_or(-1)
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

fn rom_mode(image: Image, namer: Namer, inst: Instruction) -> @parse.SourceAddressingMode {
  let size: @parse.ConstantSize = if width(inst.mode) == 2 { Byte } else { Word }
  let at = inst.pc
  // (an absolute encoding of a zero-page address stays absolute: lowering
  // reads the decoded instruction's mode)
  match inst.mode {
    "imp" | "acc" | "imm" | "rel" => source_mode(image, inst)
    "zp" | "abs" =>
      if inst.op == JMP || inst.op == JSR { source_mode(image, inst) }
      else { AbsoluteOrZeroPage(addr=namer.name(inst.arg, size, indexed=false, at~)) }
    "zpx" | "absx" => AbsoluteOrZeroPageX(addr=namer.name(inst.arg, size, indexed=true, at~))
    "zpy" | "absy" => AbsoluteOrZeroPageY(addr=namer.name(inst.arg, size, indexed=true, at~))
    // pointers: the variable holding the address
    "indx" => IndexedIndirect(addr=namer.name(inst.arg, size, indexed=true))
    "indy" => IndirectIndexed(addr=namer.name(inst.arg, size, indexed=false))
    "ind" => Indirect(addr=namer.name(inst.arg, size, indexed=false, at~))
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
  // A branch or jump with nothing pushed to an RTS that is also a computed
  // jump (returning through two bytes pushed on other paths) returns.
  let transfer_name = fn(from: Int, pc: Int) {
    let plain = facts.get(from).map(fn(f) { f.depth == Some(0) }).unwrap_or(false)
    let computed = jumps.contains(pc) && instructions.get(pc).map(fn(i) { i.op == RTS }).unwrap_or(false)
    if plain && computed { "__ret__" } else { target_name(pc) }
  }
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
      append(Inst(op=JMP, mode=AbsoluteOrZeroPage(addr=Ident(transfer_name(pc, branch_target(inst))))), pc)
    } else if inst.op == JMP && inst.mode == "ind" && rom_vector(image, inst) is Some(target) {
      append(Inst(op=JMP, mode=AbsoluteOrZeroPage(addr=Ident(target_name(target)))), pc)
    } else if inst.mode == "rel" && branch != Some(false) {
      append(Inst(op=inst.op, mode=Relative(addr=Ident(transfer_name(pc, branch_target(inst))))), pc)
    } else if inst.op == JMP && inst.mode == "abs" {
      append(Inst(op=inst.op, mode=AbsoluteOrZeroPage(addr=Ident(transfer_name(pc, jump_target(inst))))), pc)
    } else if inst.op == JSR && inst.mode == "abs" {
      append(Inst(op=inst.op, mode=AbsoluteOrZeroPage(addr=Ident(target_name(jump_target(inst))))), pc)
    } else if branch != Some(false) {
      append(Inst(op=inst.op, mode=rom_mode(image, namer, inst)), pc)
    }
    let call = inst.op == RTS && facts.get(pc).map(fn(f) { f.depth == Some(4) }).unwrap_or(false) && jumps.contains(pc)
    let falls = (call || (inst.op != JMP && inst.op != RTS)) && inst.op != RTI &&
      inst.op != BRK && !dispatches.contains(pc) && branch != Some(true)
    let next = if inst.op == JSR { image.continuation(pc, inst.next, jump_target(inst)) } else { inst.next }
    if inst.op == JSR && always_unwinds(image, jump_target(inst)) {
      // the callee returns past this routine: nothing follows the call
      append(Inst(op=JMP, mode=AbsoluteOrZeroPage(addr=Ident(trap_name(inst.next)))), pc)
    } else if inst.op == JSR && next != inst.next {
      // past the bytes the callee reads
      append(Inst(op=JMP, mode=AbsoluteOrZeroPage(addr=Ident(target_name(next)))), pc)
    } else if falls && transfer_name(pc, inst.next) == "__ret__" {
      append(Inst(op=JMP, mode=AbsoluteOrZeroPage(addr=Ident("__ret__"))), pc)
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
  { image, instructions, dispatches, roots: roots.map(fn(pc) { address_name(image, pc) }), facts, idle, windows, jumps, waits, exact: Ref(map.exact), restarts: [], source, addresses, map }
}
