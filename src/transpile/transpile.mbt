
pub(all) struct Emitter {
  output: StringBuilder
  mut indentation: Int
}

pub fn Emitter::new() -> Emitter {
  { output: StringBuilder(), indentation: 0 }
}

fn Emitter::emit(self: Emitter, s: String, newline~: Bool = true) -> Unit {
  let indent = "  ".repeat(self.indentation)
  self.output.write_string("\{indent}\{s}")

  if newline {
    self.output.write_char('\n')
  }
}

fn hex(n: Int) -> String {
  let mut n = n
  let h = StringBuilder()

  if n == 0 {
    return "0"
  }

  while n > 0 {
    let digit = n % 16
    n /= 16

    let d = match digit {
      10 => "a"
      11 => "b"
      12 => "c"
      13 => "d"
      14 => "e"
      15 => "f"
      _ => digit.to_string()
    }

    h.write_string(d)
  }

  h.to_string().rev()
}

fn bin(n: Int) -> String {
  let mut n = n
  let b = StringBuilder()

  if n == 0 {
    return "0"
  }

  while n > 0 {
    let digit = n % 2
    n /= 2

    b.write_string(digit.to_string())
  }

  b.to_string().rev().pad_start(8, '0')
}

fn show_const(base~: @parse.ConstantBase, value~: Int) -> String {
  match base {
    Decimal => value.to_string()
    Hexadecimal => "0x\{hex(value)}"
    Binary => "0b\{bin(value)}"
  }
}

fn show_expr(expr: @lower.TypedExpr) -> String {
  match expr {
    Const(base~, value~, size=_, ty=_) => show_const(base~, value~)
    Ident(name~, ty=LabelTy) => "ROM_ADDR_" + name
    Ident(name~, ty=_) => name
    Unary(op~, expr~, ty=_) => {
      let macro_ = match op {
        GetHighByte => "HIGH_BYTE"
        GetLowByte => "LOW_BYTE"
      }

      "\{macro_}(\{show_expr(expr)})"
    }
    Binary(op~, lhs~, rhs~, ty=_) => {
      let op = match op {
        Add => "+"
        Sub => "-"
      }

      "(\{show_expr(lhs)} \{op} \{show_expr(rhs)})"
    }
  }
}

pub(all) suberror EmitterError { EmitterError(String) } derive(Debug)

fn Emitter::emit_constants_h(self: Emitter, low: @lower.AsmLowering) -> Unit {
  self.emit("#ifndef SMB_CONSTANTS_H")
  self.emit("#define SMB_CONSTANTS_H")
  self.emit("")
  self.emit("#define HIGH_BYTE(x) (((x) >> 8) & 0xff)")
  self.emit("#define LOW_BYTE(x) ((x) & 0xff)")
  self.emit("")

  // in source order, with the comment that follows each definition
  for i, item in low.items {
    if item is Define(name~, ..) && low.defines.get(name) is Some(val) {
      let comment = match low.items.get(i + 1) {
        Some(Comment(comment~, is_inline=true)) => " // " + comment
        _ => ""
      }
      self.emit("#define \{name} \{show_expr(val)}\{comment}")
    }
  }

  self.emit("\n#endif")
}

fn Emitter::emit_data_h(self: Emitter, low: @lower.AsmLowering) -> Unit {
  self.emit("#ifndef SMB_DATA_H")
  self.emit("#define SMB_DATA_H")
  self.emit("")
  self.emit("#include \"stdint.h\"")
  self.emit("#include \"constants.h\"")
  self.emit("")
  let size = match low.program { Some(p) => prg_size(p); None => low.table_offset }
  self.emit("extern const uint8_t data[\{size}];")
  self.emit("#define PRG_IMAGE_SIZE \{size}")
  self.emit("")

  if low.program is Some(p) {
    let symbols = p.image.symbols.keys().to_array()
    symbols.sort()
    for name in symbols { self.emit("#define ROM_ADDR_\{name} 0x\{hex(p.image.symbols[name] & 0xffff)}") }
  }
  for label, table in low.tables_by_label {
    let address = match low.program {
      Some(p) => p.image.symbols[label] & 0xffff
      None => 0x8000 + table.offset
    }
    self.emit("#define \{label} 0x\{hex(address)}")
  }

  self.emit("")
  self.emit("#endif")
}

// The PRG ROM's bytes: the image, or every bank with a mapper.
fn prg_size(p: @image.Program) -> Int {
  if p.image.banked() { p.image.banks.contents.length() * 0x4000 } else { p.image.bytes.length() }
}

// The location of each byte of the PRG ROM in data.c order, for its labels:
// with a mapper, the banks in the window, the last one at $C000.
fn prg_location(p: @image.Program, offset: Int) -> Int {
  if !p.image.banked() { return p.image.origin + offset }
  let bank = offset / 0x4000
  if bank == p.image.banks.contents.length() - 1 { 0xc000 + offset % 0x4000 } else { @image.bank_location(bank, 0x8000 + offset % 0x4000) }
}

fn prg_byte(p: @image.Program, offset: Int) -> Int {
  if p.image.banked() { p.image.banks.contents[offset / 0x4000][offset % 0x4000] } else { p.image.bytes[offset] }
}

fn Emitter::emit_data_c(self: Emitter, low: @lower.AsmLowering) -> Unit {
  self.emit("#include \"data.h\"")
  self.emit("")

  self.emit("const uint8_t data[] = {")
  self.indentation += 1

  if low.program is Some(p) {
    let labels: Map[Int, Array[String]] = Map([])
    let notes: Map[Int, Array[String]] = Map([])
    let data_addresses: Set[Int] = Set([])
    for i, item in p.image.source {
      if item is Directive(name="db" | "dw", ..) { data_addresses.add(p.image.addresses[i]) }
    }
    for i, item in p.image.source {
      if item is Comment(comment~, is_inline~) {
        let address = if is_inline && i > 0 { p.image.addresses[i - 1] } else { p.image.addresses[i] }
        if data_addresses.contains(address) {
          if !notes.contains(address) { notes[address] = [] }
          notes[address].push(comment)
        }
      }
    }
    for i, item in p.image.source {
      if item is Label(name) {
        let pc = p.image.addresses[i]
        if !labels.contains(pc) { labels[pc] = [] }
        labels[pc].push(name)
      }
    }
    let mut offset = 0
    let size = prg_size(p)
    while offset < size {
      let pc = prg_location(p, offset)
      if p.image.banked() && offset % 0x4000 == 0 { self.emit("// bank \{offset / 0x4000}: \{p.image.banks.names[offset / 0x4000]}") }
      if labels.get(pc) is Some(names) { self.emit("// \{names.join(", ")}") }
      if notes.get(pc) is Some(comments) {
        for comment in comments { self.emit("// " + comment) }
      }
      let bytes = []
      for _ in 0..<16 {
        bytes.push("0x" + hex(prg_byte(p, offset)).pad_start(2, '0'))
        offset += 1
        if offset == size || offset % 0x4000 == 0 { break }
        let next = prg_location(p, offset)
        if labels.contains(next) || notes.contains(next) { break }
      }
      self.emit(bytes.join(", ") + ",")
    }
  } else {
    for table in low.tables {
      self.emit("// \{table.name}")
      for line in table.lines {
        match line {
          Data(data~, comment~) => self.emit(data.map(show_expr).join(", ") + ", // " + comment.val)
          Comment(comment) => self.emit("// " + comment)
        }
      }
    }
  }

  self.indentation -= 1
  self.emit("};\n")
  let size = match low.program { Some(p) => prg_size(p); None => low.table_offset }
  self.emit("const unsigned prg_image_size = \{size};")
}

// `uses_ppu_bus`: the program writes PPU registers, which a host supplies
// through ppu_bus.h (SMB's display code writes its state directly instead).
fn Emitter::emit_code_h(self: Emitter, program: @ir.Program, uses_ppu_bus~: Bool) -> Unit {
  let banked = !program.banks.is_empty()
  self.emit("#ifndef SMB_CODE_H")
  self.emit("#define SMB_CODE_H")
  self.emit("")
  self.emit("#include \"data.h\"")
  self.emit("#include \"cpu.h\"")
  if uses_ppu_bus { self.emit("#include \"ppu_bus.h\"") }
  if banked { self.emit("#include \"mapper.h\"") }
  self.emit("#include \"sound.h\"")
  self.emit("")

  if banked {
    self.emit("// PRG ROM: 16 KiB banks, the last fixed at $C000 (ROM), one of them")
    self.emit("// switched in at $8000; the cartridge's RAM at $6000.")
    self.emit("#define PRG_BANKS \{program.banks.length()}")
    self.emit("#define ROM(addr) data[(PRG_BANKS - 1) * 0x4000 + (addr) - 0xc000]")
    self.emit("#define BANK_ROM(bank, addr) data[(bank) * 0x4000 + (addr) - 0x8000]")
    self.emit("#define WINDOW_ROM(addr) prg_window[(addr) - 0x8000]")
    self.emit("#define WRAM(addr) wram[(addr) - 0x6000]")
  } else {
    self.emit("// PRG ROM read; images smaller than 32 KiB are mirrored.")
    self.emit("#define ROM(addr) data[((addr) - 0x8000) % PRG_IMAGE_SIZE]")
  }
  self.emit("")
  self.emit("void cpu_reset_entry(void);")
  self.emit("void cpu_nmi_entry(void);")
  for f in program.functions {
    if !f.helper { self.emit("void \{f.name}(void);") }
  }

  self.emit("")

  self.emit("#endif")
}

pub(all) struct File {
  name: String
  contents: String
  is_lib: Bool
}

pub fn File::write(self: File, dir~: String) -> Unit raise Error {
  @fs.write_string_to_file("\{dir}/\{self.name}", self.contents)
}

pub(all) struct Transpiler {
  files: Array[File]
}

pub(all) suberror TranspileError { UnknownNativeRoutine(String) } derive(Debug)

// `program` is the IR of `low`; routines it marks native are supplied by
// hand-written C, and only their prototypes and contracts are generated.
pub fn Transpiler::transpile(
  low: @lower.AsmLowering,
  program: @ir.Program,
  native~: Array[String] = [],
) -> Transpiler raise TranspileError {
  for name in native {
    if program.function(name) is None { raise UnknownNativeRoutine(name) }
  }
  let constants_h: File = {
    name: "constants.h",
    contents: {
      let emitter = Emitter::new()
      emitter.emit_constants_h(low)
      emitter.output.to_string()
    },
    is_lib: true,
  }

  let data_h = {
    name: "data.h",
    contents: {
      let emitter = Emitter::new()
      emitter.emit_data_h(low)
      emitter.output.to_string()
    },
    is_lib: true,
  }

  let data_c = {
    name: "data.c",
    contents: {
      let emitter = Emitter::new()
      emitter.emit_data_c(low)
      emitter.output.to_string()
    },
    is_lib: true,
  }

  fn program_c(views : Views) -> String {
    let emitter = Emitter::new()
    emitter.emit_program_c(program, low.preamble, views)
    emitter.output.to_string()
  }
  // Names of the map become views (vars.h) unless the generated code
  // already uses them as identifiers.
  let views = Views::from_map(low, identifiers(program_c(Views::empty())))
  let code_c = { name: "code.c", contents: program_c(views), is_lib: true }
  let vars_h = { name: "vars.h", contents: views.header(), is_lib: true }

  let code_h = {
    name: "code.h",
    contents: {
      let emitter = Emitter::new()
      emitter.emit_code_h(program, uses_ppu_bus=code_c.contents.contains("ppu_bus_"))
      emitter.output.to_string()
    },
    is_lib: true,
  }

  {
    files: [
      constants_h,
      data_h, data_c,
      code_h, code_c,
    ] + (if views.list.is_empty() { [] } else { [vars_h] }),
  }
}
