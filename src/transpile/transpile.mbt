
pub(all) struct Emitter {
  output: StringBuilder
  mut fold: Bool // fold load/store pairs (off for the differential reference)
  mut indentation: Int
  mut jsr_return_index: Int
  local_labels: Set[String]
  used_labels: Set[String]
  mut function_name: String
}

pub fn Emitter::new() -> Emitter {
  {
    output: StringBuilder::new(),
    fold: true,
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

// `uses_ppu_bus`: the program writes PPU registers, which a host supplies
// through ppu_bus.h (SMB's display code writes its state directly instead).
fn Emitter::emit_code_h(self: Emitter, low: @lower.AsmLowering, uses_ppu_bus~: Bool) -> Unit {
  self.emit("#ifndef SMB_CODE_H")
  self.emit("#define SMB_CODE_H")
  self.emit("")
  self.emit("#include \"data.h\"")
  self.emit("#include \"cpu.h\"")
  if uses_ppu_bus { self.emit("#include \"ppu_bus.h\"") }
  self.emit("#include \"apu.h\"")
  self.emit("")

  self.emit("// PRG ROM read; images smaller than 32 KiB are mirrored.")
  self.emit("#define ROM(addr) data[((addr) - 0x8000) % PRG_IMAGE_SIZE]")
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
      // labels (code and data tables) resolve to their assembled address
      None => match low.program {
        Some(p) => match p.image.symbols.get(name) {
          Some(address) => Some({ min: address, max: address })
          None => None
        }
        None => None
      }
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
  let value = match target {
    A => "a"
    X => "x"
    Y => "y"
  }
  write_value(mode, value, low)
}

// A store of `target` (a C expression) through an addressing mode.
fn write_value(mode: @lower.AsmAddressingMode, target: String, low: @lower.AsmLowering) -> String {

  fn rewrite(addr: @lower.TypedExpr, suffix: String, zero_page: Bool) -> String {
    let expression = "\{show_expr(addr)}\{suffix}"
    let address = if zero_page { "(uint8_t)(\{expression})" } else { expression }
    if zero_page { return "ram[\{address}] = \{target};" }
    match compute_range(addr, mode, low) {
      Some({ min, max }) if min >= 0 && max < 0x800 => return "ram[\{address}] = \{target};"
      Some({ min, max }) if min >= 0 && max < 0x2000 => return "ram[(\{address}) & 0x7ff] = \{target};"
      Some({ min, max }) if min == max => {
        if min >= 0x2000 && min < 0x4000 {
          return "ppu_bus_write(0x\{hex(0x2000 + (min & 7))}, \{target});"
        }
        if min == 0x4014 { return "ppu_bus_oam_dma(\{target});" }
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

// Where an instruction's operand lives, proven from its address range.
priv enum Operand {
  Value(String) // immediate
  Ram(String) // a ram[] lvalue
  Rom(String) // the address of a PRG ROM read
  Bus(String) // an address that needs the bus (I/O, pointers, unknown)
}

fn operand(mode: @lower.AsmAddressingMode, low: @lower.AsmLowering) -> Operand? {
  fn absolute(expr: @lower.TypedExpr, index: String) -> Operand {
    let base = show_expr(expr)
    let address = if index == "" { base } else { "\{base} + \{index}" }
    match compute_range(expr, mode, low) {
      Some({ min, max }) if min >= 0 && max < 0x800 => Ram("ram[\{address}]")
      Some({ min, max }) if min >= 0 && max < 0x2000 => Ram("ram[(\{address}) & 0x7ff]")
      Some({ min, max }) if min >= 0x8000 && max < 0x10000 => Rom(address)
      _ => Bus(if index == "" { address } else { "(uint16_t)(\{address})" })
    }
  }
  match mode {
    Immediate(expr) => Some(Value(show_expr(expr)))
    ZeroPage(expr) => Some(Ram("ram[\{show_expr(expr)}]"))
    ZeroPageX(expr) => Some(Ram("ram[(uint8_t)(\{show_expr(expr)} + x)]"))
    ZeroPageY(expr) => Some(Ram("ram[(uint8_t)(\{show_expr(expr)} + y)]"))
    Absolute(expr) => Some(absolute(expr, ""))
    AbsoluteX(expr) => Some(absolute(expr, "x"))
    AbsoluteY(expr) => Some(absolute(expr, "y"))
    IndirectIndexed(expr) => Some(Bus("indirect_y_addr(\{show_expr(expr)})"))
    IndexedIndirect(expr) => Some(Bus("indirect_x_addr(\{show_expr(expr)})"))
    _ => None
  }
}

fn Operand::read(self: Operand) -> String {
  match self {
    Value(text) | Ram(text) => text
    Rom(address) => "ROM(\{address})"
    Bus(address) => "read_byte(\{address})"
  }
}

// Reading a bus address can have side effects (I/O), so it happens once.
fn Operand::is_pure(self: Operand) -> Bool {
  !(self is Bus(_))
}

fn zn_updates(value: String, live: @lower.FlagLiveness) -> Array[String] {
  let out = []
  if live.zero { out.push("zero_flag = \{value} == 0;") }
  if live.negative { out.push("neg_flag = (\{value} & 0x80) != 0;") }
  out
}

fn register_name(op: @parse.OpCode) -> String {
  match op {
    LDX | CPX | INX | DEX | TAX | TSX => "x"
    LDY | CPY | INY | DEY | TAY => "y"
    _ => "a"
  }
}

// The value a read-modify-write instruction updates: a ram[] lvalue, or a
// temporary read through the bus and written back (pushed into `out` before
// and returned after the operation).
fn rmw_target(mode: @lower.AsmAddressingMode, low: @lower.AsmLowering, out: Array[String]) -> (String, Array[String])? {
  match operand(mode, low) {
    Some(Ram(lvalue)) => Some((lvalue, []))
    Some(Rom(address)) | Some(Bus(address)) => {
      out.push("uint16_t address = \{address};")
      out.push("uint8_t m = read_byte(address);")
      Some(("m", ["dynamic_ram_write(address, m);"]))
    }
    _ => None
  }
}

// Plain C for one instruction over the register and flag variables, updating
// only the flags that are live afterwards. None for an unsupported form.
fn native_instruction(
  op: @parse.OpCode,
  mode: @lower.AsmAddressingMode,
  id: Int,
  low: @lower.AsmLowering,
) -> String? {
  let live = low.flag_live_after(id)
  let out: Array[String] = []
  // Binds the operand to `m` when it is read more than once or is impure.
  fn bind(operand: Operand, uses: Int) -> String {
    if uses > 1 || (uses == 1 && !operand.is_pure()) {
      out.push("uint8_t m = \{operand.read()};")
      "m"
    } else {
      operand.read()
    }
  }
  // Statements that declare a temporary get their own scope.
  fn block() -> String {
    let declares = out.iter().any(fn(s) {
      s.has_prefix("uint8_t ") || s.has_prefix("uint16_t ") || s.has_prefix("bool ")
    })
    if declares { "{ " + out.join(" ") + " }" } else { out.join(" ") }
  }
  match op {
    LDA | LDX | LDY => {
      let r = register_name(op)
      let source = match operand(mode, low) { Some(o) => o; None => return None }
      out.push("\{r} = \{source.read()};")
      out.append(zn_updates(r, live))
    }
    AND | ORA | EOR => {
      let source = match operand(mode, low) { Some(o) => o; None => return None }
      let symbol = match op { AND => "&"; ORA => "|"; _ => "^" }
      out.push("a \{symbol}= \{source.read()};")
      out.append(zn_updates("a", live))
    }
    ADC => {
      let source = match operand(mode, low) { Some(o) => o; None => return None }
      if !live.carry && !live.overflow {
        out.push("a = (uint8_t)(a + \{source.read()} + carry_flag);")
      } else {
        let m = bind(source, if live.overflow { 2 } else { 1 })
        out.push("uint16_t sum = a + \{m} + carry_flag;")
        if live.overflow { out.push("overflow_flag = (~(a ^ \{m}) & (a ^ sum) & 0x80) != 0;") }
        if live.carry { out.push("carry_flag = sum > 0xff;") }
        out.push("a = (uint8_t)sum;")
      }
      out.append(zn_updates("a", live))
    }
    SBC => {
      let source = match operand(mode, low) { Some(o) => o; None => return None }
      if !live.carry && !live.overflow {
        out.push("a = (uint8_t)(a - \{source.read()} - !carry_flag);")
      } else {
        let m = bind(source, if live.overflow { 2 } else { 1 })
        out.push("uint16_t diff = (uint16_t)(a - \{m} - !carry_flag);")
        if live.overflow { out.push("overflow_flag = ((a ^ \{m}) & (a ^ diff) & 0x80) != 0;") }
        if live.carry { out.push("carry_flag = diff <= 0xff;") }
        out.push("a = (uint8_t)diff;")
      }
      out.append(zn_updates("a", live))
    }
    CMP | CPX | CPY => {
      let r = register_name(op)
      let source = match operand(mode, low) { Some(o) => o; None => return None }
      let uses = (if live.carry { 1 } else { 0 }) + (if live.zero { 1 } else { 0 }) + (if live.negative { 1 } else { 0 })
      if uses == 0 {
        if !source.is_pure() { out.push("(void)\{source.read()};") }
      } else {
        let m = bind(source, uses)
        if live.carry { out.push("carry_flag = \{r} >= \{m};") }
        if live.zero { out.push("zero_flag = \{r} == \{m};") }
        if live.negative { out.push("neg_flag = ((\{r} - \{m}) & 0x80) != 0;") }
      }
    }
    BIT => {
      let source = match operand(mode, low) { Some(o) => o; None => return None }
      let uses = (if live.zero { 1 } else { 0 }) + (if live.negative { 1 } else { 0 }) + (if live.overflow { 1 } else { 0 })
      if uses == 0 {
        if !source.is_pure() { out.push("(void)\{source.read()};") }
      } else {
        let m = bind(source, uses)
        if live.zero { out.push("zero_flag = (a & \{m}) == 0;") }
        if live.negative { out.push("neg_flag = (\{m} & 0x80) != 0;") }
        if live.overflow { out.push("overflow_flag = (\{m} & 0x40) != 0;") }
      }
    }
    TAX | TAY => { out.push("\{register_name(op)} = a;"); out.append(zn_updates(register_name(op), live)) }
    TXA => { out.push("a = x;"); out.append(zn_updates("a", live)) }
    TYA => { out.push("a = y;"); out.append(zn_updates("a", live)) }
    TSX => { out.push("x = sp;"); out.append(zn_updates("x", live)) }
    INX | INY => { out.push("\{register_name(op)}++;"); out.append(zn_updates(register_name(op), live)) }
    DEX | DEY => { out.push("\{register_name(op)}--;"); out.append(zn_updates(register_name(op), live)) }
    INC | DEC => {
      let (target, write_back) = match rmw_target(mode, low, out) { Some(t) => t; None => return None }
      out.push(if op == INC { "\{target}++;" } else { "\{target}--;" })
      out.append(write_back)
      out.append(zn_updates(target, live))
    }
    ASL | LSR | ROL | ROR => {
      let (target, write_back) = match mode {
        Accumulator => ("a", [])
        _ => match rmw_target(mode, low, out) { Some(t) => t; None => return None }
      }
      let carry_out = if op == ASL || op == ROL { "(\{target} & 0x80) != 0" } else { "(\{target} & 1) != 0" }
      let shifted = match op {
        ASL => "(uint8_t)(\{target} << 1)"
        LSR => "\{target} >> 1"
        ROL => "(uint8_t)(\{target} << 1 | carry_flag)"
        _ => "(uint8_t)(\{target} >> 1 | carry_flag << 7)"
      }
      let rotates = op == ROL || op == ROR
      if live.carry && rotates {
        out.push("bool carry = \{carry_out};")
        out.push("\{target} = \{shifted};")
        out.push("carry_flag = carry;")
      } else {
        if live.carry { out.push("carry_flag = \{carry_out};") }
        out.push("\{target} = \{shifted};")
      }
      out.append(write_back)
      out.append(zn_updates(target, live))
    }
    PHA => out.push("ram[0x100 + sp--] = a;")
    PLA => { out.push("a = ram[0x100 + ++sp];"); out.append(zn_updates("a", live)) }
    CLD => out.push("decimal_flag = false;")
    SED => out.push("decimal_flag = true;")
    CLI => out.push("interrupt_disabled = false;")
    SEI => out.push("interrupt_disabled = true;")
    CLV => if live.overflow { out.push("overflow_flag = false;") }
    NOP => ()
    PHP => out.push("php();")
    PLP => out.push("plp();")
    _ => return None
  }
  Some(block())
}

// The register and value of an instruction that only loads a register and
// whose flags are dead afterwards: LDA/LDX/LDY and the transfers.
fn plain_load(item: @lower.AsmItem, low: @lower.AsmLowering) -> (String, String)? {
  guard item is Inst(id~, op~, mode~, ..) && id >= 0 else { return None }
  let live = low.flag_live_after(id)
  if live.zero || live.negative { return None }
  match op {
    LDA | LDX | LDY => match operand(mode, low) {
      Some(source) => Some((register_name(op), source.read()))
      None => None
    }
    TAX => Some(("x", "a"))
    TAY => Some(("y", "a"))
    TXA => Some(("a", "x"))
    TYA => Some(("a", "y"))
    _ => None
  }
}

// `load R; store R` where R is dead after the store: store the value itself.
fn fused_store(load: @lower.AsmItem, store: @lower.AsmItem, low: @lower.AsmLowering) -> String? {
  guard plain_load(load, low) is Some((register, value)) else { return None }
  guard store is Inst(id~, op=STA | STX | STY as op, mode~, ..) && id >= 0 else { return None }
  let stored = match op { STA => "a"; STX => "x"; _ => "y" }
  if stored != register { return None }
  let dead = match register {
    "a" => !low.register_live_after(id).a
    "x" => !low.register_live_after(id).x
    _ => !low.register_live_after(id).y
  }
  if dead { Some(write_value(mode, value, low)) } else { None }
}

fn item_comment(item: @lower.AsmItem) -> String? {
  match item {
    Inst(comment~, ..) => comment
    _ => None
  }
}

fn Emitter::emit_items(self: Emitter, items: ArrayView[@lower.AsmItem], low: @lower.AsmLowering) -> Unit {
  let mut i = 0
  while i < items.length() {
    if self.fold && i + 1 < items.length() {
      if fused_store(items[i], items[i + 1], low) is Some(code) {
        let comments = [item_comment(items[i]), item_comment(items[i + 1])].filter_map(fn(c) { c })
        self.emit(if comments.is_empty() { code } else { "\{code} // \{comments.join("; ")}" })
        i += 2
        continue
      }
    }
    self.emit_item(items[i], low)
    i += 1
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

                "\{target}();"
              }
              (RTS, _) => "return;"
              _ => {
                match native_instruction(op, mode, id, low) {
                  Some(code) => code
                  None => abort("Unsupported instruction: \{@debug.to_string(op)} \{args}")
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
        self.emit("default: cpu_unresolved_jump(cpu_dispatch_target); return; // index outside the proven table")
        self.indentation -= 1
        self.emit("}")
      }

      DoWhile(cond~, body~, comment~) => {
        self.emit("do {")
        self.indentation += 1
        self.emit_items(body[:], low)
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

        self.emit_items(then[:], low)

        self.indentation -= 1
        
        match otherwise {
          Some(otherwise) => {
            self.emit("} else {")
            self.indentation += 1

            self.emit_items(otherwise[:], low)

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
  // Comments preceding the entry label document the routine: keep them
  // above the C function rather than as its first statements.
  let mut body = 0
  while subroutine.items.get(body) is Some(Comment(text)) {
    self.emit("// \{text}")
    body += 1
  }
  if interface_comment(low, subroutine.name) is Some(text) { self.emit(text) }
  self.emit("void \{rename_label(subroutine.name)}(void) {")
  self.indentation = 1

  self.emit_items(subroutine.items[body:], low)

  self.indentation = 0
  self.emit("}\n")
}

fn show_names(names: Array[String]) -> String {
  if names.is_empty() { "none" } else { names.join(", ") }
}

// Inputs are registers read before being written; outputs are registers and
// flags the routine may change that some caller reads after it returns.
fn interface_comment(low: @lower.AsmLowering, name: String) -> String? {
  let inputs = low.routine_inputs(name).names()
  let outputs = low.routine_register_outputs(name).names()
  outputs.append(low.routine_flag_outputs(name).names())
  if inputs.is_empty() && outputs.is_empty() { return None }
  Some("// in: \{show_names(inputs)}; out: \{show_names(outputs)}")
}

fn Emitter::emit_code_c(self: Emitter, low: @lower.AsmLowering, native: Set[String]) -> Unit {
  self.emit("#include \"code.h\"")
  self.emit("")
  if !low.preamble.is_empty() {
    for line in low.preamble { self.emit("// \{line}") }
    self.emit("")
  }

  if low.program is Some(p) {
    let helpers: Map[Int, @image.Dispatch] = Map([])
    for dispatch in p.dispatches.values() { helpers[dispatch.helper] = dispatch }
    let addresses = helpers.keys().to_array()
    addresses.sort()
    for address in addresses {
      let dispatch = helpers[address]
      let name = @image.address_name(p.image, address)
      self.emit("// Inline-table dispatch proven by evaluating \{name}; its effects run here unchanged.")
      self.emit("static uint16_t \{name}(uint16_t return_address) {")
      self.indentation += 1
      self.emit("cpu_push_return_address(return_address);")
      for step in dispatch.steps {
        let inst = step.inst
        if inst.op == JMP { continue }
        if inst.mode == "rel" {
          // Every accepted index took this direction; any other state traps.
          let cond = show_cond_opcode(inst.op)
          let other = if step.taken { inst.next } else { @image.branch_target(inst) }
          let deviates = if step.taken { "!(\{cond})" } else { cond }
          self.emit("if (\{deviates}) { cpu_unresolved_jump(0x\{hex(other)}); }")
          continue
        }
        let mut comment: String? = None
        for i, item in p.image.source {
          if p.image.addresses[i] == inst.pc && item is Inst(_) {
            if p.image.source.get(i + 1) is Some(Comment(comment=text, is_inline=true)) { comment = Some(text) }
          }
        }
        let mode = try low.lower_mode(@image.source_mode(p.image, inst), op=Some(inst.op)) catch { _ => abort("Invalid dispatcher instruction") }
        self.emit_item(@lower.AsmItem::Inst(id=-1, op=inst.op, mode~, comment~), low)
      }
      let exit = match dispatch.exit {
        IndirectJump(pointer) => "read_indirect_word(0x\{hex(pointer)})"
        ReturnJump => "cpu_return_target()"
      }
      self.emit("return cpu_dispatch_target = \{exit};")
      self.indentation -= 1
      self.emit("}\n")
    }
  }

  for sub in low.flat_subroutines {
    let name = rename_label(sub.name)
    if native.contains(name) {
      // Hand-written replacement: it must leave every register and flag its
      // callers read exactly as the original routine would.
      let outputs = low.routine_register_outputs(sub.name).names()
      outputs.append(low.routine_flag_outputs(sub.name).names())
      self.emit("// \{name}: native implementation, not generated.")
      self.emit("// It may read: \{show_names(low.routine_inputs(sub.name).names())}.")
      self.emit("// It must produce: \{show_names(outputs)}.\n")
    } else {
      self.emit_flat_subroutine(sub, low)
    }
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

pub(all) suberror TranspileError { UnknownNativeRoutine(String) } derive(Debug)

// `native` lists routines whose bodies are supplied by hand-written C; only
// their prototypes are generated.
// `fold` combines load/store pairs; the differential test disables it to
// build its reference.
pub fn Transpiler::transpile(
  low: @lower.AsmLowering,
  native~: Array[String] = [],
  fold~: Bool = true,
) -> Transpiler raise TranspileError {
  let routines: Set[String] = Set([])
  for sub in low.flat_subroutines { routines.add(rename_label(sub.name)) }
  for name in native {
    if !routines.contains(name) { raise UnknownNativeRoutine(name) }
  }
  let native = Set::from_array(native)
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

  let code_c = {
    name: "code.c",
    contents: {
      let emitter = Emitter::new()
      emitter.fold = fold
      emitter.emit_code_c(low, native)
      emitter.output.to_string()
    },
    is_lib: true,
  }

  let code_h = {
    name: "code.h",
    contents: {
      let emitter = Emitter::new()
      emitter.emit_code_h(low, uses_ppu_bus=code_c.contents.contains("ppu_bus_"))
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
