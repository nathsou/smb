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
  Table // bytes of PRG ROM from the address
} derive(Eq)

priv struct View {
  name : String // lower case
  symbol : String // the address constant
  kind : ViewKind
  address : Int
  length : Int
  type_name : String // "" when untyped
  description : String
}

priv struct Views {
  by_symbol : Map[String, View]
  list : Array[View]
  origin : Int // of the PRG image
  stack : Bool // `stack` names page one
  types : Map[String, @image.MapType]
}

fn Views::empty() -> Views {
  { by_symbol: Map([]), list: [], origin: 0x8000, stack: false, types: Map([]) }
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
      Table if entry.address >= p.image.origin => Table
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
  { ..views, stack: !views.list.is_empty() && !taken("stack") && !seen.contains("stack") }
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
    let word = StringBuilder::new()
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
    if v.kind != Scalar && v.kind != Table && v.address <= address && address < v.address + v.length {
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
  if v.kind == Scalar && (offset != 0 || !index.is_empty()) {
    match self.containing(v.address + offset) {
      Some(array) => {
        offset = v.address + offset - array.address
        v = array
      }
      None => return None
    }
  }
  if wraps is Some(kind) && v.kind != kind { return None }
  let fits = match v.kind {
    Scalar => true
    ZeroPageArray | RamArray | MirroredArray => offset >= 0 && offset < v.length
    // before the table only with an index (`table - 1 + y`), never before
    // the image
    Table => (offset >= 0 && offset < v.length) || (offset < 0 && !index.is_empty() && v.address + offset >= self.origin)
  }
  if fits { Some({ view: v, offset, index }) } else { None }
}

fn Views::header(self : Views) -> String {
  let out = StringBuilder::new()
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
  for v in self.list {
    let comment = if v.description == "" { "" } else { " // " + v.description }
    let text = match v.kind {
      Scalar => "\{v.name} ram[\{v.symbol}]"
      ZeroPageArray => "\{v.name}(i) ram[(uint8_t)(\{v.symbol} + (i))]"
      RamArray => "\{v.name} (&ram[\{v.symbol}])"
      MirroredArray => "\{v.name}(i) ram[(\{v.symbol} + (i)) & 0x7ff]"
      Table => "\{v.name} (&ROM(\{v.symbol}))"
    }
    out.write_string("#define \{text}\{comment}\n")
  }
  out.write_string("\n#endif\n")
  out.to_string()
}

// A type of the map as C constants: an anonymous enum, so that its name
// stays free for the variable of the same name.
pub fn type_definition(t : @image.MapType) -> String {
  let out = StringBuilder::new()
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
