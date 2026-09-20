
pub(all) struct Emitter {
  output: StringBuilder
  mut indentation: Int
  mut jsr_return_index: Int
  local_labels: Set[String]
  used_labels: Set[String]
  mut function_name: String
}

pub fn Emitter::new() -> Emitter {
  {
    output: StringBuilder::new(),
    indentation: 0,
    jsr_return_index: 0,
    local_labels: Set([]),
    used_labels: Set([]),
    function_name: "",
  }
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
  let h = StringBuilder::new()

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
  let b = StringBuilder::new()

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

  for name, val in low.defines {
    self.emit("#define \{name} \{show_expr(val)}")
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
  let size = match low.program { Some(p) => p.image.bytes.length(); None => low.table_offset }
  self.emit("extern const uint8_t data[\{size}];")
  self.emit("#define PRG_IMAGE_SIZE \{size}")
  self.emit("")

  if low.program is Some(p) {
    let symbols = p.image.symbols.keys().to_array()
    symbols.sort()
    for name in symbols { self.emit("#define ROM_ADDR_\{name} 0x\{hex(p.image.symbols[name])}") }
  }
  for label, table in low.tables_by_label {
    let address = match low.program {
      Some(p) => p.image.symbols[label]
      None => 0x8000 + table.offset
    }
    self.emit("#define \{label} 0x\{hex(address)}")
  }

  self.emit("")
  self.emit("#endif")
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
    while offset < p.image.bytes.length() {
      let pc = p.image.origin + offset
      if labels.get(pc) is Some(names) { self.emit("// \{names.join(", ")}") }
      if notes.get(pc) is Some(comments) {
        for comment in comments { self.emit("// " + comment) }
      }
      let bytes = []
      for _ in 0..<16 {
        bytes.push("0x" + hex(p.image.bytes[offset]).pad_start(2, '0'))
        offset += 1
        if offset == p.image.bytes.length() || labels.contains(p.image.origin + offset) || notes.contains(p.image.origin + offset) { break }
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
}

fn Emitter::emit_code_h(self: Emitter, low: @lower.AsmLowering) -> Unit {
  self.emit("#ifndef SMB_CODE_H")
  self.emit("#define SMB_CODE_H")
  self.emit("")
  self.emit("#include \"data.h\"")
  self.emit("#include \"instructions.h\"")
  self.emit("#include \"ppu.h\"")
  self.emit("#include \"apu.h\"")
  self.emit("")

  self.emit("void cpu_reset_entry(void);")
  self.emit("void cpu_nmi_entry(void);")
  for sub in low.subroutines {
    self.emit("void \{rename_label(sub.name)}(void);")
  }

  self.emit("")

  self.emit("#endif")
}

fn rename_label(label: String) -> String {
  if label.has_suffix("__sub") {
    label.split("__sub").iter().next().unwrap().to_owned()
  } else {
    label
  }
}

fn Emitter::gen_jump(self: Emitter, target: String, low: @lower.AsmLowering) -> String {
  let renamed_label = rename_label(target)

  match target {
    "__ret__" => "return;"
    _ => {
      if !self.local_labels.contains(target) && low.targets.subroutines.contains(target) {
        "\{renamed_label}(); return;"
      } else {
        "goto \{renamed_label};"
      }
    }
  }
}

fn show_cond_opcode(op: @parse.OpCode) -> String {
  match op {
    BCC => "!carry_flag"
    BCS => "carry_flag"
    BEQ => "zero_flag"
    BNE => "!zero_flag"
    BMI => "neg_flag"
    BPL => "!neg_flag"
    BVC => "!overflow_flag"
    BVS => "overflow_flag"
    _ => abort("unreachable")
  }
}

fn flag_helper_suffix(op: @parse.OpCode, id: Int, low: @lower.AsmLowering) -> String {
  let writes = @lower.flag_writes(op)
  let live_writes = writes.intersect(low.flag_live_after(id))

  if writes.is_none() {
    ""
  } else if !live_writes.carry && !live_writes.zero && !live_writes.negative {
    ""
  } else {
    let mut suffix = "_f"

    if live_writes.carry {
      suffix += "c"
    }
    if live_writes.zero {
      suffix += "z"
    }
    if live_writes.negative {
      suffix += "n"
    }

    suffix
  }
}

fn is_dead_flag_only_instruction(
  op: @parse.OpCode,
  mode: @lower.AsmAddressingMode,
  id: Int,
  low: @lower.AsmLowering,
) -> Bool {
  match (op, mode) {
    (BIT, Absolute(expr) | ZeroPage(expr)) => {
      match compute_expr_range(expr, low) {
        Some({ min, max }) if min == max && ((min >= 0 && max < 0x2000) || (min >= 0x8000 && max < 65536)) => {
          @lower.flag_writes(op).intersect(low.flag_live_after(id)).is_none()
        }
        _ => false
      }
    }
    (CMP | CPX | CPY, Immediate(_)) => {
      @lower.flag_writes(op).intersect(low.flag_live_after(id)).is_none()
    }
    _ => false
  }
}

priv enum Reg { A; X; Y }
priv struct Range { min: Int; max: Int }

impl Add for Range with add(self: Range, other: Range) -> Range {
  { min: self.min + other.min, max: self.max + other.max }
}

impl Sub for Range with sub(self: Range, other: Range) -> Range {
  { min: self.min - other.max, max: self.max - other.min }
}

fn compute_expr_range(expr: @lower.TypedExpr, low: @lower.AsmLowering) -> Range? {
  match expr {
    Const(value~, ..) => Some({ min: value, max: value })
    Ident(name~, ..) => match low.defines.get(name) {
      Some(expr) => compute_expr_range(expr, low)
      None => None
    }
    Binary(op~, lhs~, rhs~, ..) => {
      let lhs_range = compute_expr_range(lhs, low)
      let rhs_range = compute_expr_range(rhs, low)

      match (lhs_range, rhs_range) {
        (Some(lhs_range), Some(rhs_range)) => match op {
          Add => Some(lhs_range + rhs_range)
          Sub => Some(lhs_range - rhs_range)
        }
        _ => None
      }
    }
    _ => None
  }
}

fn compute_range(expr: @lower.TypedExpr, mode: @lower.AsmAddressingMode, low: @lower.AsmLowering) -> Range? {
  let mode_range = match mode {
    ZeroPageX(_) | AbsoluteX(_) | ZeroPageY(_) | AbsoluteY(_) => { min: 0, max: 0xff }
    _ => { min: 0, max: 0 }
  }

  match compute_expr_range(expr, low) {
    Some(expr_range) => Some(expr_range + mode_range)
    None => None
  }
}

fn write_byte(mode: @lower.AsmAddressingMode, target: Reg, low: @lower.AsmLowering) -> String {
  let target = match target {
    A => "a"
    X => "x"
    Y => "y"
  }

  fn rewrite(addr: @lower.TypedExpr, suffix: String, zero_page: Bool) -> String {
    let expression = "\{show_expr(addr)}\{suffix}"
    let address = if zero_page { "(uint8_t)(\{expression})" } else { expression }
    if zero_page { return "ram[\{address}] = \{target};" }
    match compute_range(addr, mode, low) {
      Some({ min, max }) if min >= 0 && max < 0x800 => return "ram[\{address}] = \{target};"
      Some({ min, max }) if min >= 0 && max < 0x2000 => return "ram[(\{address}) & 0x7ff] = \{target};"
      Some({ min, max }) if min == max => {
        if min >= 0x2000 && min < 0x4000 {
          return match min & 7 {
            0 => "ppu_ctrl = \{target};"
            1 => "ppu_mask = \{target};"
            3 => "oam_addr = \{target};"
            4 => "oam[oam_addr++] = \{target};"
            5 => "ppu_write_scroll(\{target});"
            6 => "ppu_write_address(\{target});"
            7 => "ppu_write_data(\{target});"
            _ => "ppu_write_register((uint16_t)(\{address}), \{target});"
          }
        }
        if min == 0x4014 { return "ppu_transfer_oam((uint16_t)(\{target} << 8));" }
        if min == 0x4016 { return "write_joypad1(\{target}); write_joypad2(\{target});" }
        if min >= 0x4000 && min < 0x4020 { return "apu_write(\{address}, \{target});" }
      }
      _ => ()
    }
    "dynamic_ram_write((uint16_t)(\{address}), \{target});"
  }
  match mode {
    ZeroPage(addr) | Absolute(addr) => rewrite(addr, "", false)
    ZeroPageX(addr) => rewrite(addr, " + x", true)
    ZeroPageY(addr) => rewrite(addr, " + y", true)
    AbsoluteX(addr) => rewrite(addr, " + x", false)
    AbsoluteY(addr) => rewrite(addr, " + y", false)
    IndirectIndexed(addr) => "dynamic_ram_write(indirect_y_addr(\{show_expr(addr)}), \{target});"
    IndexedIndirect(addr) => "dynamic_ram_write(indirect_x_addr(\{show_expr(addr)}), \{target});"
    _ => abort("Invalid store addressing mode")
  }
}

fn Emitter::emit_item(self: Emitter, item: @lower.AsmItem, low: @lower.AsmLowering) -> Unit {
    match item {
      Label(name) => {
        // do not output labels for subroutines
        let is_subroutine_label = !self.local_labels.contains(name);

        if !is_subroutine_label && self.used_labels.contains(name) {
          self.emit("\n\{rename_label(name)}:")
        } else if name != self.function_name && !name.has_prefix("loc_") {
          self.emit("// \{rename_label(name)}:")
        }
      }
      Inst(id~, op~, mode~, comment~) => {
        let stmt = match (op, mode) {
          (PLP, _) => "plp();"
          (TXS, _) => "sp = x;"
          (RTI, _) => "return; // <rti>"
          (CLC, _) => {
            if low.flag_live_after(id).carry {
              "carry_flag = false;"
            } else {
              ""
            }
          }
          (SEC, _) => {
            if low.flag_live_after(id).carry {
              "carry_flag = true;"
            } else {
              ""
            }
          }
          (STA, _) => write_byte(mode, A, low)
          (STX, _) => write_byte(mode, X, low)
          (STY, _) => write_byte(mode, Y, low)
          _ => {
            let args = match mode {
              Accumulator => ""
              Implied => ""
              Immediate(expr) => "\{show_expr(expr)}"
              Relative(expr) => "\{show_expr(expr)}"
              ZeroPage(expr) => "\{show_expr(expr)}"
              ZeroPageX(expr) => "\{show_expr(expr)}"
              ZeroPageY(expr) => "\{show_expr(expr)}"
              Absolute(expr) => "\{show_expr(expr)}"
              AbsoluteX(expr) => "\{show_expr(expr)}"
              AbsoluteY(expr) => "\{show_expr(expr)}"
              Indirect(expr) => "\{show_expr(expr)}"
              IndexedIndirect(expr) => "\{show_expr(expr)}"
              IndirectIndexed(expr) => "\{show_expr(expr)}"
            }

            let is_relative = match mode {
              Relative(_) => true
              _ => false
            }

            match (op, is_relative) {
              (BCC | BCS | BEQ | BMI | BNE | BPL | BVC | BVS , true) => {
                let cond = show_cond_opcode(op)

                let target = match mode {
                  Relative(Ident(name~, ..)) => name
                  _ => abort("unreachable")
                }

                "if (\{cond}) { \{self.gen_jump(target, low)} }"
              }
              (JMP, _) => {
                if low.program is Some(p) && p.idle.contains(id) {
                  "cpu_yield(0x\{hex(id)}); return;"
                } else {
                  let target = match mode { Absolute(Ident(name~, ..)) => name; _ => abort("Unresolved jump") }
                  self.gen_jump(target, low)
                }
              }
              (JSR, _) => {
                let target = match mode {
                  Absolute(Ident(name~, ty=_)) => rename_label(name)
                  _ => ""
                }

                if id >= 0 && low.program is Some(_) {
                  "cpu_call_begin(0x\{hex(id + 2)}); \{target}(); cpu_call_end(0x\{hex(id + 2)});"
                } else { "\{target}();" }
              }
              (RTS, _) => "return;"
              _ => {
                if is_dead_flag_only_instruction(op, mode, id, low) {
                  ""
                } else {
                  let helper = "\{@debug.to_string(op).to_lower()}\{mode.suffix()}\{flag_helper_suffix(op, id, low)}"
                  "\{helper}(\{args});"
                }
              }
            }
          }
        }

        let stmt = match comment {
          Some(comment) => {
            "\{stmt} // \{comment}"
          }
          None => stmt
        }

        self.emit(stmt)
      }
      JumpEngine(jump_table~, site~) => {
        let program = low.program.unwrap()
        let dispatch = program.dispatches[site]
        let helper = @image.address_name(program.image, dispatch.helper)
        self.emit("switch (\{helper}(0x\{hex(site + 2)})) {")
        self.indentation += 1
        let emitted: Set[Int] = Set([])
        for idx, label in jump_table {
          let address = dispatch.targets[idx]
          if !emitted.contains(address) {
            self.emit("case 0x\{hex(address)}: \{rename_label(label)}(); return;")
            emitted.add(address)
          }
        }
        self.emit("default: cpu_unresolved_jump(read_indirect_word(0x\{hex(dispatch.pointer)})); return;")
        self.indentation -= 1
        self.emit("}")
      }

      DoWhile(cond~, body~, comment~) => {
        self.emit("do {")
        self.indentation += 1
        for child in body { self.emit_item(child, low) }
        self.indentation -= 1
        let comment = match comment { Some(text) => " // " + text; None => "" }
        self.emit("} while (\{show_cond_opcode(cond)});\{comment}")
      }
      If(comparison~, cond~, then~, otherwise~, comment~) => {
        let cond = match comparison {
          Some((register, operand)) => {
            let relation = match cond { BEQ => "=="; BNE => "!="; BCC => "<"; BCS => ">="; _ => abort("Invalid comparison") }
            "\{register} \{relation} \{show_expr(operand)}"
          }
          None => show_cond_opcode(cond)
        }

        match comment {
          Some(comment) => {
            self.emit("// \{comment}")
          }
          None => ()
        }

        self.emit("if (\{cond}) {")

        self.indentation += 1

        for item in then {
          self.emit_item(item, low)
        }

        self.indentation -= 1
        
        match otherwise {
          Some(otherwise) => {
            self.emit("} else {")
            self.indentation += 1

            for item in otherwise {
              self.emit_item(item, low)
            }

            self.indentation -= 1
            self.emit("}")
          }
          None => self.emit("}")
        }
      }
      Comment(comment) => {
        self.emit("// \{comment}")
      }
    }
}

fn Emitter::emit_flat_subroutine(self: Emitter, subroutine: @lower.AsmFlatSubroutine, low: @lower.AsmLowering) -> Unit {
  self.function_name = subroutine.name
  self.used_labels.clear()
  fn collect(items: Array[@lower.AsmItem]) {
    for item in items {
      match item {
        Inst(id~, op=JMP | BCC | BCS | BEQ | BNE | BMI | BPL | BVC | BVS,
          mode=Relative(Ident(name~, ..)) | Absolute(Ident(name~, ..)), ..) => {
          if !(low.program is Some(p) && p.idle.contains(id)) { self.used_labels.add(name) }
        }
        DoWhile(body~, ..) => collect(body)
        If(then~, otherwise~, ..) => {
          collect(then)
          if otherwise is Some(other) { collect(other) }
        }
        _ => ()
      }
    }
  }
  collect(subroutine.items)
  self.local_labels.clear()
  for sub in low.subroutines {
    if sub.name == subroutine.name {
      for label in sub.internal_labels { self.local_labels.add(label) }
    }
  }
  self.indentation = 0
  self.emit("void \{rename_label(subroutine.name)}(void) {")
  self.indentation = 1

  for item in subroutine.items {
    self.emit_item(item, low)
  }

  self.indentation = 0
  self.emit("}\n")
}

fn Emitter::emit_code_c(self: Emitter, low: @lower.AsmLowering) -> Unit {
  self.emit("#include \"code.h\"")
  self.emit("")

  if low.program is Some(p) {
    let helpers: Map[Int, @image.Dispatch] = Map([])
    for dispatch in p.dispatches.values() { helpers[dispatch.helper] = dispatch }
    let addresses = helpers.keys().to_array()
    addresses.sort()
    for address in addresses {
      let dispatch = helpers[address]
      let name = @image.address_name(p.image, address)
      self.emit("// Return-address indexed dispatch; effects preserved from the original helper.")
      self.emit("static uint16_t \{name}(uint16_t return_address) {")
      self.indentation += 1
      self.emit("cpu_call_begin(return_address);")
      for inst in dispatch.setup {
        let mode = try low.lower_mode(@image.source_mode(p.image, inst), op=Some(inst.op)) catch { _ => abort("Invalid dispatcher instruction") }
        let mut comment: String? = None
        for i, item in p.image.source {
          if p.image.addresses[i] == inst.pc && item is Inst(_) {
            if p.image.source.get(i + 1) is Some(Comment(comment=text, is_inline=true)) { comment = Some(text) }
          }
        }
        self.emit_item(@lower.AsmItem::Inst(id=-1, op=inst.op, mode~, comment~), low)
      }
      self.emit("return read_indirect_word(0x\{hex(dispatch.pointer)});")
      self.indentation -= 1
      self.emit("}\n")
    }
  }

  for sub in low.flat_subroutines {
    self.emit_flat_subroutine(sub, low)
  }
  if low.program is Some(p) {
    let base = if p.image.bytes.length() == 16384 { p.image.origin + 0x3ffa } else { 0xfffa }
    let nmi = try @image.address_name(p.image, p.image.word(base)) catch { _ => abort("Missing NMI vector") }
    self.emit("void cpu_reset_entry(void) { \{p.roots[0]}(); }")
    self.emit("void cpu_nmi_entry(void) { \{nmi}(); }")
  }
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

pub fn Transpiler::transpile(low: @lower.AsmLowering) -> Transpiler {
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

  let code_h = {
    name: "code.h",
    contents: {
      let emitter = Emitter::new()
      emitter.emit_code_h(low)
      emitter.output.to_string()
    },
    is_lib: true,
  }

  let code_c = {
    name: "code.c",
    contents: {
      let emitter = Emitter::new()
      emitter.emit_code_c(low)
      emitter.output.to_string()
    },
    is_lib: true,
  }

  {
    files: [
      constants_h,
      data_h, data_c,
      code_h, code_c,
    ],
  }
}
