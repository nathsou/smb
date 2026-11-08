// RAM variables and ROM tables by name.
//
// With a ROM map, the generated C reads and writes memory through the map's
// names instead of `ram[ADDRESS]`: a variable is the byte at its address, an
// array or a table the bytes from its address. They are views of `ram` and
// `data` (vars.h), so the memory layout, and what the program does with it,
// stay those of the machine: the game indexes across neighbouring arrays and
// clears ranges of them, which separate storage would break.
//
//   ram[WORLD_NUMBER] = 0          ->  world_number = 0
//   ram[VRAM_LIST1 + 1 + y]        ->  vram_list1[1 + y]
//   ram[(uint8_t)(ENEMY_Y + x)]    ->  enemy_y(x)
//   ram[(RANDOM + x) & 0x7ff]      ->  random(x)
//   ROM(POPUP_TILES + 1 + x)       ->  popup_tiles[1 + x]
//   ram[0x100 + sp]                ->  stack[sp]
//
// Arrays an index can carry out of their page or out of RAM are
// function-like: in page zero the address wraps within the page, as indexed
// zero-page addressing does on the 6502, and near the end of RAM it wraps
// to its start, as the mirrors of RAM do.
//
// A view is a macro in lower case, the map's own spelling. It is left out
// when the name is also an identifier of the generated code (a routine, a
// temporary, a register, a runtime function), and its accesses stay as
// addresses.

priv enum ViewKind {
  Scalar // one byte of RAM
  ZeroPageArray // bytes of page zero from the address, wrapping within it
  RamArray // bytes of RAM from the address
  MirroredArray // bytes of RAM from the address, wrapping to its start
  Table // bytes of PRG ROM from the address (the fixed bank's, with a mapper)
  BankTable(Int) // bytes of a switchable bank from its address in the window
  WindowTable // bytes of the window from the address, in the bank mapped
  WramScalar // one byte of the cartridge's RAM
  WramArray // bytes of the cartridge's RAM from the address
} derive(Eq)

// Whether accesses to `space` read or write the bytes a view names.
fn ViewKind::names(self : ViewKind, space : @ir.Mem) -> Bool {
  match (self, space) {
    (Scalar | ZeroPageArray | RamArray | MirroredArray, Ram) => true
    (Table, Rom) | (WindowTable, Window) => true
    (BankTable(a), Bank(b)) => a == b
    (WramScalar | WramArray, Wram) => true
    _ => false
  }
}


priv struct View {
  name : String // lower case
  symbol : String // the address constant
  kind : ViewKind
  address : Int
  length : Int
  type_name : String // "" when untyped
  description : String
}

// A record (the map's `record`): arrays of RAM indexed alike as the fields
// of one structure laid over RAM, `enemy(x).kind` for `enemy_kind(x)`.
// Each member sits at its array's address, relative to the first, so
// `enemy(i)` is the structure at that address plus i: the bytes stay where
// they are. Unlike the arrays' views, an index does not wrap within page
// zero or RAM; the program's indices stay within their arrays
// (--check-scratch checks it).
priv struct Record {
  name : String
  base : Int // the lowest field's address
  members : Array[(String, Int, String)] // field, address, the array's type
  description : String
}

priv struct Views {
  by_symbol : Map[String, View]
  list : Array[View]
  origin : Int // of the PRG image
  stack : Bool // `stack` names page one
  types : Map[String, @image.MapType]
  records : Array[Record]
  // the record and field each array of a record is
  fields : Map[String, (Int, String)]
  // names of the scratch bytes by routine (the map's `scratch`): byte, name,
  // description
  scratch : Map[String, Array[(Int, String, String)]]
}

fn Views::empty() -> Views {
  { by_symbol: Map([]), list: [], origin: 0x8000, stack: false, types: Map([]), records: [], fields: Map([]), scratch: Map([]) }
}

// Identifiers a view must not shadow besides those of the generated code:
// C keywords and the names inside the runtime's macros.
let c_reserved : Array[String] = [
  "auto", "break", "case", "char", "const", "continue", "default", "do", "double",
  "else", "enum", "extern", "float", "for", "goto", "if", "inline", "int", "long",
  "register", "restrict", "return", "short", "signed", "sizeof", "static", "struct",
  "switch", "typedef", "union", "unsigned", "void", "volatile", "while", "bool",
  "true", "false", "data", "ram", "addr", "i",
]

// The views a ROM map gives, except those named in `reserved`. ROM tables
// are views of the PRG image only from its first byte (a 16 KiB image is
// mirrored below its origin).
fn Views::from_map(low : @lower.AsmLowering, reserved : Set[String]) -> Views {
  guard low.program is Some(p) else { return Views::empty() }
  let taken = fn(name : String) { reserved.contains(name) || c_reserved.contains(name) }
  let views = { ..Views::empty(), origin: p.image.origin, types: Map::from_array(p.map.types.map(fn(t) { (t.name, t) })) }
  let banked = p.image.banked()
  let seen : Set[String] = Set([])
  for entry in p.map.entries {
    let name = entry.name.to_lower()
    if taken(name) || seen.contains(name) { continue }
    let end = entry.address + (if entry.length > 0 { entry.length } else { 1 })
    let kind = match entry.kind {
      Var if entry.length == 0 && end <= 0x800 => Scalar
      Var if end <= 0x100 => ZeroPageArray
      Var if entry.address + 0xff < 0x800 => RamArray
      Var if end <= 0x800 => MirroredArray
      Var if banked && entry.address >= 0x6000 && end <= 0x8000 => if entry.length == 0 { WramScalar } else { WramArray }
      Table | Code if entry.bank >= 0 => BankTable(entry.bank)
      Table | Code if banked && @image.in_window(entry.address) => WindowTable
      Table | Code if entry.address >= p.image.origin => Table
      _ => continue
    }
    seen.add(name)
    let view = {
      name,
      symbol: entry.name,
      kind,
      address: entry.address,
      length: entry.length,
      type_name: entry.type_name,
      description: entry.description,
    }
    views.by_symbol[entry.name] = view
    views.list.push(view)
  }
  // records of arrays of RAM, each field named without the record's prefix
  for r in p.map.records {
    if taken(r.name) || seen.contains(r.name) { continue }
    let members = []
    for symbol in r.fields {
      guard p.map.entries.iter().find_first(fn(e) { e.name == symbol }) is Some(e) else { continue }
      let lower = symbol.to_lower()
      let field = if lower.has_prefix(r.name + "_") { lower.view(start_offset=r.name.length() + 1).to_owned() } else { lower }
      members.push((field, e.address, e.type_name, symbol))
    }
    members.sort_by(fn(a, b) { a.1.compare(b.1) })
    let base = members[0].1
    let index = views.records.length()
    for m in members { views.fields[m.3] = (index, m.0) }
    views.records.push({ name: r.name, base, members: members.map(fn(m) { (m.0, m.1, m.2) }), description: r.description })
    seen.add(r.name)
  }
  // a record's arrays are read and written as its fields, not by their own
  // views; a field named like a remaining view keeps its array's name
  let remaining = views.list.filter(fn(v) { !views.fields.contains(v.symbol) })
  let names : Set[String] = Set(remaining.map(fn(v) { v.name }))
  for i, r in views.records {
    let members = r.members.map(fn(m) {
      let symbol = views.fields.iter().find_first(fn(e) { e.1.0 == i && e.1.1 == m.0 }).map(fn(e) { e.0 }).unwrap()
      let field = if names.contains(m.0) || c_reserved.contains(m.0) { symbol.to_lower() } else { m.0 }
      views.fields[symbol] = (i, field)
      (field, m.1, m.2)
    })
    views.records[i] = { ..r, members }
  }
  for v in views.list {
    if views.fields.contains(v.symbol) { views.by_symbol.remove(v.symbol) }
  }
  // scratch names, unless an identifier, a view, a record or a record's
  // member already has the name (a #define would rewrite `.member`)
  let members : Set[String] = Set([])
  for r in views.records {
    members.add(r.name)
    for m in r.members { members.add(m.0) }
  }
  for e in p.map.scratch {
    if taken(e.name) || names.contains(e.name) || members.contains(e.name) { continue }
    match views.scratch.get(e.routine) {
      Some(list) => list.push((e.byte, e.name, e.description))
      None => views.scratch[e.routine] = [(e.byte, e.name, e.description)]
    }
  }
  {
    ..views,
    list: remaining,
    stack: !remaining.is_empty() && !taken("stack") && !seen.contains("stack"),
  }
}

// An address as a field of a record: the record, the field, and the
// index (a constant offset and other terms), wrapping or not.
fn Views::record_place(self : Views, address : @ir.Expr) -> (Record, String, Int, Array[@ir.Expr])? {
  if self.records.is_empty() { return None }
  let sum = match address {
    Unary(Byte, inner) => inner
    Binary(And, inner, Const(0x7ff)) => inner
    _ => address
  }
  let mut found : (Int, String)? = None
  let mut offset = 0
  let mut symbols = 0
  let index = []
  let mut ok = true
  fn walk(e : @ir.Expr) -> Unit {
    match e {
      Binary(Add, l, r) => {
        walk(l)
        walk(r)
      }
      Binary(Sub, l, Const(v)) => {
        walk(l)
        offset -= v
      }
      Const(v) => offset += v
      Sym(name, v) => {
        symbols += 1
        match self.fields.get(name) {
          Some(f) if found is None => found = Some(f)
          _ => offset += v
        }
      }
      Binary(Sub, _, _) => ok = false
      _ => index.push(e)
    }
  }
  walk(sum)
  guard ok && symbols == 1 && found is Some((r, field)) else { return None }
  let record = self.records[r]
  // In a record whose fields are consecutive bytes (an array of
  // structures, the OAM's sprites), the byte reached is the field at its
  // place in the structure: $0203 + 2 is the next sprite's tile.
  let stride = record.members.length()
  let dense = record.members.mapi(fn(i, m) { m.1 == record.base + i }).iter().all(fn(b) { b })
  if dense && stride > 1 {
    let at = record.members.iter().find_first(fn(m) { m.0 == field }).unwrap().1 - record.base + offset
    let place = ((at % stride) + stride) % stride
    return Some((record, record.members[place].0, at - place, index))
  }
  Some((record, field, offset, index))
}

// The type of a record's field.
fn Record::type_of(self : Record, field : String) -> String {
  match self.members.iter().find_first(fn(m) { m.0 == field }) {
    Some(m) => m.2
    None => ""
  }
}

// Identifiers of C text, outside comments.
fn identifiers(text : String) -> Set[String] {
  let found : Set[String] = Set([])
  for line in text.split("\n") {
    let line = line.to_owned()
    let code = match line.find("//") {
      Some(i) => line.view(end_offset=i).to_owned()
      None => line
    }
    let word = StringBuilder()
    let mut length = 0
    for ch in code {
      if ch.is_ascii_alphabetic() || ch == '_' || (length > 0 && ch >= '0' && ch <= '9') {
        word.write_char(ch)
        length += 1
      } else if length > 0 {
        found.add(word.to_string())
        word.reset()
        length = 0
      }
    }
    if length > 0 { found.add(word.to_string()) }
  }
  found
}

// An address as a view, a constant offset into it and other added terms.
priv struct Place {
  view : View
  offset : Int
  index : Array[@ir.Expr]
}

// The array view that holds `address`, the one starting nearest below it.
fn Views::containing(self : Views, address : Int) -> View? {
  let mut found : View? = None
  for v in self.list {
    if (v.kind is (ZeroPageArray | RamArray | MirroredArray | WramArray)) && v.address <= address && address < v.address + v.length {
      if found is Some(f) && f.address >= v.address { continue }
      found = Some(v)
    }
  }
  found
}

fn Views::place(self : Views, address : @ir.Expr) -> Place? {
  // indexed zero-page addressing wraps within the page, an index past
  // the end of RAM to its start
  let (wraps, sum) = match address {
    Unary(Byte, inner) => (Some(ZeroPageArray), inner)
    Binary(And, inner, Const(0x7ff)) => (Some(MirroredArray), inner)
    _ => (None, address)
  }
  let mut view : View? = None
  let mut offset = 0
  let mut symbols = 0
  let index = []
  let mut ok = true
  fn walk(e : @ir.Expr) -> Unit {
    match e {
      Binary(Add, l, r) => {
        walk(l)
        walk(r)
      }
      Binary(Sub, l, Const(v)) => {
        walk(l)
        offset -= v
      }
      Const(v) => offset += v
      Sym(name, v) => {
        symbols += 1
        match self.by_symbol.get(name) {
          Some(found) if view is None => view = Some(found)
          _ => offset += v
        }
      }
      Binary(Sub, _, _) => ok = false
      _ => index.push(e)
    }
  }
  walk(sum)
  // a second symbol folded into the offset would hide its name
  guard ok && symbols == 1 && view is Some(named) else { return None }
  let mut v = named
  if (v.kind == Scalar || v.kind == WramScalar) && (offset != 0 || !index.is_empty()) {
    match self.containing(v.address + offset) {
      Some(array) => {
        offset = v.address + offset - array.address
        v = array
      }
      None => return None
    }
  }
  if wraps is Some(kind) && v.kind != kind { return None }
  // an index that does not wrap (an absolute encoding in page zero) is not
  // what the wrapping views give
  if wraps is None && !index.is_empty() && (v.kind == ZeroPageArray || v.kind == MirroredArray) { return None }
  let fits = match v.kind {
    Scalar | WramScalar => true
    ZeroPageArray | RamArray | MirroredArray | WramArray => offset >= 0 && offset < v.length
    // before the table only with an index (`table - 1 + y`), never before
    // the image (or the window)
    Table => (offset >= 0 && offset < v.length) || (offset < 0 && !index.is_empty() && v.address + offset >= self.origin)
    BankTable(_) | WindowTable => (offset >= 0 && offset < v.length) || (offset < 0 && !index.is_empty() && v.address + offset >= 0x8000)
  }
  if fits { Some({ view: v, offset, index }) } else { None }
}

fn Views::header(self : Views) -> String {
  let out = StringBuilder()
  out.write_string("#ifndef SMB_VARS_H\n#define SMB_VARS_H\n\n")
  out.write_string("// RAM variables and ROM tables by name, as views of ram[] and data[]:\n")
  out.write_string("// a variable is the byte at its address, an array or a table the bytes\n")
  out.write_string("// from its address. Arrays an index can carry out of their page are\n")
  out.write_string("// function-like: in page zero the address wraps within the page, as\n")
  out.write_string("// indexed zero-page addressing does, and past the end of RAM to its\n")
  out.write_string("// start, as the mirrors of RAM do.\n")
  out.write_string("// Included by code.c only, after every other header.\n\n")
  for t in self.types.values() { out.write_string(type_definition(t) + "\n") }
  if self.stack { out.write_string("#define stack (&ram[0x100]) // the 6502 stack, page one\n") }
  for r in self.records {
    let about = if r.description == "" { "" } else { ": " + r.description }
    out.write_string("\n// \{r.name}\{about}. Its fields are arrays of RAM indexed alike, laid\n")
    out.write_string("// out at their addresses: \{r.name}(i) is the structure at $\{r.base.to_string(radix=16)} + i.\n")
    out.write_string("struct \{r.name}_record {\n")
    let mut at = r.base
    let mut gap = 0
    for m in r.members {
      if m.1 > at {
        out.write_string("  uint8_t gap\{gap}[\{m.1 - at}];\n")
        gap += 1
      }
      out.write_string("  uint8_t \{m.0}; // $\{m.1.to_string(radix=16).pad_start(4, '0')}\n")
      at = m.1 + 1
    }
    out.write_string("};\n")
    out.write_string("#define \{r.name}(i) (*(struct \{r.name}_record *)&ram[0x\{r.base.to_string(radix=16)} + (i)])\n")
  }
  for v in self.list {
    let comment = if v.description == "" { "" } else { " // " + v.description }
    let text = match v.kind {
      Scalar => "\{v.name} ram[\{v.symbol}]"
      ZeroPageArray => "\{v.name}(i) ram[(uint8_t)(\{v.symbol} + (i))]"
      RamArray => "\{v.name} (&ram[\{v.symbol}])"
      MirroredArray => "\{v.name}(i) ram[(\{v.symbol} + (i)) & 0x7ff]"
      Table => "\{v.name} (&ROM(\{v.symbol}))"
      BankTable(bank) => "\{v.name} (&BANK_ROM(\{bank}, \{v.symbol}))"
      WindowTable => "\{v.name} (&WINDOW_ROM(\{v.symbol}))"
      WramScalar => "\{v.name} WRAM(\{v.symbol})"
      WramArray => "\{v.name} (&WRAM(\{v.symbol}))"
    }
    out.write_string("#define \{text}\{comment}\n")
  }
  out.write_string("\n#endif\n")
  out.to_string()
}

// A type of the map as C constants: an anonymous enum, so that its name
// stays free for the variable of the same name.
pub fn type_definition(t : @image.MapType) -> String {
  let out = StringBuilder()
  let what = if t.flags { "bits" } else { "values" }
  let about = if t.description == "" { "" } else { ": " + t.description }
  out.write_string("// \{t.name} \{what}\{about}\n")
  out.write_string("enum {\n")
  for m in t.members {
    let value = if t.flags { "0x" + m.1.to_string(radix=16).pad_start(2, '0') } else { m.1.to_string() }
    out.write_string("  \{m.0} = \{value},\n")
  }
  out.write_string("};\n")
  out.to_string()
}

// A constant as a value of a type: an enumeration's value by name, bits
// as the names of those set or, for a mask with most set, of those clear.
fn Views::constant(self : Views, type_name : String, v : Int, mask~ : Bool = false) -> String? {
  guard self.types.get(type_name) is Some(t) else { return None }
  if !t.flags {
    return t.members.iter().find_first(fn(m) { m.1 == v }).map(fn(m) { m.0 })
  }
  fn bits(v : Int) -> String? {
    let names = []
    let mut rest = v
    for m in t.members {
      if (rest & m.1) == m.1 {
        names.push(m.0)
        rest = rest & m.1.lnot()
      }
    }
    if rest == 0 && !names.is_empty() { Some(names.join(" | ")) } else { None }
  }
  if v <= 0 || v > 255 { return None }
  let mut set = 0
  let mut n = v
  while n > 0 {
    set += n & 1
    n = n >> 1
  }
  if set <= 4 || !mask { return bits(v) }
  match bits(v.lnot() & 0xff) {
    Some(clear) => Some(if clear.contains("|") { "~(\{clear})" } else { "~" + clear })
    None => bits(v)
  }
}
