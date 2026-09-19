
pub(all) struct AsmLowering {
  items: Array[@parse.SourceItem]
  program: @image.Program?
  lowered: Array[AsmItem]
  mut next_instruction_id: Int
  defines: Map[String, TypedExpr]
  jump_target_labels: Set[String]
  tables: Array[AsmTable]
  tables_by_label: Map[String, AsmTable]
  mut table_offset: Int
  targets: JumpTargets
  basic_blocks: Array[BasicBlock]
  bb_by_label: Map[String, BasicBlock]
  subroutines: Array[AsmSubroutine]
  flat_subroutines: Array[AsmFlatSubroutine]
  flag_liveness: Map[Int, FlagLiveness]
} derive (Debug)

pub suberror LoweringError { LoweringError(String) } derive(Debug)

pub fn AsmLowering::new(items: Array[@parse.SourceItem], program~: @image.Program? = None) -> AsmLowering {
  {
    items,
    program,
    lowered: [],
    next_instruction_id: 0,
    defines: {},
    tables: [],
    table_offset: 0, // in bytes
    jump_target_labels: Set([]),
    tables_by_label: {},
    targets: { jump_targets: Set([]), subroutines: Set([]), externals: Set([]) },
    basic_blocks: [],
    bb_by_label: {},
    subroutines: [],
    flat_subroutines: [],
    flag_liveness: {},
  }
}

fn AsmLowering::detect_table(self: AsmLowering, name: String, index: Int) -> AsmTable? raise LoweringError {
  let lines: Array[AsmTableLine] = []
  let mut elem_size: @parse.ConstantSize? = None
  let mut length = 0
  let mut i = index + 1

  while i < self.items.length() {
    match self.items[i] {
      Directive(name="db" | "dw" as name, args~) => {
        let directive_elem_size: @parse.ConstantSize = if name == "db" { Byte } else { Word }

        match elem_size {
          Some(size) => {
            if size != directive_elem_size {
              elem_size = Some(Byte)
            }
          }
          None => elem_size = {
            Some(directive_elem_size)
          }
        }

        length += args.length()

        let data = []

        for arg in args {
          data.push(self.type_expr(arg))
        }

        lines.push(Data(data~, comment=Ref("")))
      }
      Comment(comment~, is_inline~) => {
        if is_inline {
          match lines.last() {
            Some(Data(data=_, comment=comment_ref)) => {
              comment_ref.val = comment
            }
            _ => ()
          }
        } else {
          lines.push(Comment(comment))
        }
      }
      _ => break
    }

    i += 1
  }

  match elem_size {
    Some(elem_size) => {
      Some({
        name,
        offset: self.table_offset,
        size: length * elem_size.byte_size(),
        lines,
        elem_size,
        items_count: i - index,
      })
    }
    None => None
  }
}

fn AsmLowering::collect_labels_and_tables(self: AsmLowering) -> Unit raise LoweringError {
  let mut idx = 0

  while idx < self.items.length() {
    let item = self.items[idx]
    let mut idx_increment = 1

    match item {
      Label(name) => {
        let aliases = [name]
        let mut last = idx
        let mut scan = idx + 1
        while scan < self.items.length() {
          match self.items[scan] {
            Label(alias_) => { aliases.push(alias_); last = scan }
            Comment(_) => ()
            _ => break
          }
          scan += 1
        }
        match self.detect_table(name, last) {
          Some(table) => {
            self.tables.push(table)
            self.table_offset += table.size
            for alias_ in aliases { self.tables_by_label[alias_] = table }
            idx_increment = last - idx + table.items_count
          }
          None => { self.jump_target_labels.add(name) }
        }
      }
      _ => ()
    }

    idx += idx_increment
  }
}

fn AsmLowering::type_expr(self: AsmLowering, expr: @parse.SourceExpr) -> TypedExpr raise LoweringError {
  match expr {
    Const(base~, value~, size~) => {
      let ty = match size {
        Byte => ConstTy(Byte)
        Word => ConstTy(Word)
      }

      Const(base~, value~, size~, ty~)
    }
    Ident(name) => {
      match self.tables_by_label.get(name) {
        Some(table) => Ident(name~, ty=ArrayTy(table.elem_size))
        None => match self.defines.get(name) {
          Some(expr) => Ident(name~, ty=expr.ty())
          None => Ident(name~, ty=LabelTy)
        }
      }
    }
    Unary(op~, expr~) => {
      let expr = self.type_expr(expr)
      Unary(op~, expr~, ty=expr.ty())
    }
    Binary(op~, lhs~, rhs~) => {
      let lhs = self.type_expr(lhs)
      let rhs = self.type_expr(rhs)
      Binary(op~, lhs~, rhs~, ty=lhs.ty())
    }
  }
}

fn AsmLowering::collect_defines(self: AsmLowering) -> Unit raise LoweringError {
  for item in self.items {
    match item {
      Define(name~, rhs~) => {
        self.defines[name] = self.type_expr(rhs)
      }
      _ => ()
    }
  }
}

pub fn AsmLowering::lower_mode(self: AsmLowering, mode: @parse.SourceAddressingMode, op~: @parse.OpCode? = None) -> AsmAddressingMode raise LoweringError {
  match mode {
            Accumulator => Accumulator
            Implied => Implied
            Immediate(operand) => Immediate(self.type_expr(operand))
            Relative(addr~) => Relative(self.type_expr(addr))
            AbsoluteOrZeroPage(addr~) | AbsoluteOrZeroPageX(addr~) | AbsoluteOrZeroPageY(addr~) => {
              let numeric = match self.program {
                Some(p) => try Some(p.image.eval(addr)) catch { _ => None }
                None => None
              }
              let addr = self.type_expr(addr)
              let size: @parse.ConstantSize = match addr.ty() {
                ConstTy(size) => size
                ArrayTy(_) | LabelTy => Word
              }

              let size = match numeric {
                Some(v) if v >= 0 && v < 256 => {
                  let kind = match mode { AbsoluteOrZeroPage(_) => "zp"; AbsoluteOrZeroPageX(_) => "zpx"; _ => "zpy" }
                  if op is Some(opcode) && @image.encoding(opcode, kind) == None { @parse.Word } else { @parse.Byte }
                }
                Some(_) => @parse.Word
                None => size
              }
              match (mode, size) {
                (AbsoluteOrZeroPage(_), Byte) => ZeroPage(addr)
                (AbsoluteOrZeroPage(_), Word) => Absolute(addr)
                (AbsoluteOrZeroPageX(_), Byte) => ZeroPageX(addr)
                (AbsoluteOrZeroPageX(_), Word) => AbsoluteX(addr)
                (AbsoluteOrZeroPageY(_), Byte) => ZeroPageY(addr)
                (AbsoluteOrZeroPageY(_), Word) => AbsoluteY(addr)
                _ => raise LoweringError("Invalid addressing mode")
              }
            }
            Indirect(addr~) => Indirect(self.type_expr(addr))
            IndexedIndirect(addr~) => IndexedIndirect(self.type_expr(addr))
            IndirectIndexed(addr~) => IndirectIndexed(self.type_expr(addr))
          }
}

fn AsmLowering::collect_instructions(self: AsmLowering) -> Unit raise LoweringError {
  let mut inside_table = false
  let mut item_idx = 0

  while item_idx < self.items.length() {
    let item = self.items[item_idx]
    let mut idx_increment = 1

    match item {
      Label(name) => {
        if !self.tables_by_label.contains(name) {
          // this label is a jump target if it's not a table label
          
          self.lowered.push(Label(name))
          inside_table = false
        } else {
          inside_table = true
        }
      }
      Inst(op~, mode~) => {
        if op == JSR && (match self.program { Some(p) => p.dispatches.contains(p.addresses[item_idx]); None => false }) {
          let jump_table: Array[String] = []
          inside_table = true

          for i in (item_idx + 1)..<self.items.length() {
            match self.items[i] {
              Directive(name="dw", args~) => {
                for arg in args {
                  match arg {
                    Ident(label) => {
                      if !self.jump_target_labels.contains(label) {
                        raise LoweringError("Unknown label in jump table")
                      }

                      jump_table.push(label)
                    }
                    _ => raise LoweringError("Expected label in jump table")
                  }
                }
              }
              Comment(_) => ()
              _ => {
                idx_increment = i - item_idx
                break
              }
            }
          }

          self.lowered.push(JumpEngine(jump_table~, site=self.program.unwrap().addresses[item_idx]))
        } else {
          let addr_mode = self.lower_mode(mode, op=Some(op))

          let comment = match self.items.get(item_idx + 1) {
            Some(Comment(comment~, is_inline=true)) => Some(comment)
            _ => None
          }

          let id = match self.program { Some(p) => p.addresses[item_idx]; None => self.next_instruction_id }
          self.next_instruction_id += 1
          self.lowered.push(Inst(id~, op~, mode=addr_mode, comment~))
        }
      }
      Comment(comment~, is_inline=false) => {
        if !inside_table {
          self.lowered.push(Comment(comment))
        }
      }
      Directive(name="db" | "dw", ..) => {
        if !inside_table {
          () // Unlabelled padding is not an executable source instruction.
        }
      }
      _ => ()
    }

    item_idx += idx_increment
  }
}

pub(all) struct JumpTargets {
  jump_targets: Set[String]
  subroutines: Set[String]
  externals: Set[String]
} derive (Debug)

fn AsmLowering::collect_jump_targets(self: AsmLowering) -> Unit {
  for item in self.lowered {
    match item {
      Inst(
        op=JMP | JSR | BCC | BCS | BEQ | BMI | BNE | BPL | BVC | BVS as op,
        mode=Relative(Ident(name=label, ..)) | Absolute(Ident(name=label, ..)),
        ..
      ) => {
        if op == JSR {
          self.targets.subroutines.add(label)
        } else {
          self.targets.jump_targets.add(label)
        }
      }
      JumpEngine(jump_table~, ..) => {
        for label in jump_table {
          self.targets.subroutines.add(label)
        }
      }
      Label(name) => {
        // manually annotated subroutines
        if name.has_suffix("__sub") {
          self.targets.subroutines.add(name)
        }
      }
      _ => ()
    }
  }
}

fn JumpTargets::contains(self: JumpTargets, label: String) -> Bool {
  self.jump_targets.contains(label) || self.subroutines.contains(label)
}

fn AsmLowering::next_non_comment_item_index(self: AsmLowering, start: Int) -> Int? {
  for i in start..<self.lowered.length() {
    match self.lowered[i] {
      Comment(_) => ()
      _ => return Some(i)
    }
  }

  None
}

fn AsmLowering::collect_basic_block_leaders(self: AsmLowering) -> Array[Int] {
  if self.program is Some(p) {
    for root in p.roots { self.targets.subroutines.add(root) }
  }
  self.collect_jump_targets()
  let leader_indices = []

  fn insert_leader_index(idx: Int?) {
    match idx {
      Some(index) => {
        let mut idx = index
        while idx > 0 && self.lowered[idx - 1] is Comment(_) { idx -= 1 }
        // Keep preceding block documentation with its labelled entry.
        // avoid duplicates
        if leader_indices.is_empty() || leader_indices[leader_indices.length() - 1] != idx {
          leader_indices.push(idx)
        }
      }
      None => {
        ()
      }
    }
  }

  // the first non-comment instruction is always a leader
  insert_leader_index(self.next_non_comment_item_index(0))
  
  for i in 0..<self.lowered.length() {
    match self.lowered[i] {
      // the target of a jump instruction is a leader
      Label(label) => {
        if self.targets.contains(label) {
          insert_leader_index(Some(i))
        }
      }
      Inst(op~, ..) => {
        if op.is_branching() {
          // the next non-comment instruction after a branching instruction is a leader
          insert_leader_index(self.next_non_comment_item_index(i + 1))
        }
      }
      JumpEngine(_) => insert_leader_index(self.next_non_comment_item_index(i + 1))
      _ => ()
    }
  }

  if !leader_indices.is_empty() {
    leader_indices.push(self.lowered.length())
  }
  leader_indices
}

priv struct ItemInfo {
  item: AsmItem
}

fn BasicBlock::last_non_comment_item(self: BasicBlock) -> ItemInfo? {
  let mut i = self.items.length() - 1

  while i >= 0 {
    match self.items[i] {
      Comment(_) => ()
      item => return Some({ item, })
    }

    i -= 1
  }

  None
}

fn BasicBlock::has_fallthrough(self: BasicBlock) -> Bool raise LoweringError {
  let last_item = self.last_non_comment_item().unwrap_or_error(LoweringError("Empty basic block"))

  match last_item.item {
    Inst(op~, ..) => {
      match op {
        RTI | RTS | BRK | JMP => false
        _ => true
      }
    }
    _ => false
  }
}

fn AsmLowering::collect_basic_blocks(self: AsmLowering) -> Unit raise LoweringError {
  let leader_indices = self.collect_basic_block_leaders()

  // Starting from a leader, the set of all following instructions until and not including the next leader
  // is the basic block corresponding to the starting leader.
  for i in 1..<leader_indices.length() {
    let prev_leader = leader_indices[i - 1]
    let curr_leader = leader_indices[i]

    let bb = {
      id: self.basic_blocks.length(),
      name: None,
      items: [],
      predecessors: [],
      successors: [],
      is_subroutine_entry: false,
      subroutine: None,
    }

    for j in prev_leader..<curr_leader {
      match self.lowered[j] {
        Label(label) => {
          bb.name = Some(label)
          self.bb_by_label[label] = bb
        }
        _ => ()
      }

      bb.items.push(self.lowered[j])
    }

    self.basic_blocks.push(bb)
  }

  fn connect(a: BasicBlock, b: BasicBlock, is_fallthrough: Bool) -> Unit {
    if !a.successors.iter().any(fn(link) { link.bb.id == b.id }) {
      a.successors.push({ bb: b, is_fallthrough })
    }

    if !b.predecessors.iter().any(fn(link) { link.bb.id == a.id }) {
      b.predecessors.push({ bb: a, is_fallthrough })
    }
  }

  // connect basic blocks
  for bb in self.basic_blocks {
    let last_item = bb.last_non_comment_item().unwrap_or_error(LoweringError("Empty basic block"))

    match last_item.item {
      Inst(
        op=JMP | BCC | BCS | BEQ | BMI | BNE | BPL | BVC | BVS,
        mode=Relative(Ident(name=label, ..)) | Absolute(Ident(name=label, ..)),
        ..
      ) => {
        match self.bb_by_label.get(label) {
          Some(target_bb) => {
            connect(bb, target_bb, false)
          }
          None => {
            if label != "__ret__" { // allow special labels
              raise LoweringError("Unknown jump target: \{label}")
            }
          }
        }
      }
      JumpEngine(jump_table~, ..) => {
        for label in jump_table {
          match self.bb_by_label.get(label) {
            Some(target_bb) => {
              connect(bb, target_bb, false)
            }
            None => {
              if label != "__ret__" { // allow special labels
                raise LoweringError("Unknown jump target: \{label}")
              }
            }
          }
        }
      }
      _ => ()
    }

    // check for fallthrough to the next basic block
    if bb.has_fallthrough() && bb.id + 1 < self.basic_blocks.length() {
      connect(bb, self.basic_blocks[bb.id + 1], true)
    }
  }
}

pub fn flag_reads(op: @parse.OpCode) -> FlagLiveness {
  match op {
    ADC | SBC | ROL | ROR => { carry: true, zero: false, negative: false, overflow: false }
    BCC | BCS => { carry: true, zero: false, negative: false, overflow: false }
    BEQ | BNE => { carry: false, zero: true, negative: false, overflow: false }
    BMI | BPL => { carry: false, zero: false, negative: true, overflow: false }
    BVC | BVS => { ..FlagLiveness::none(), overflow: true }
    JSR | PHP => FlagLiveness::all()
    _ => FlagLiveness::none()
  }
}

pub fn flag_writes(op: @parse.OpCode) -> FlagLiveness {
  match op {
    ADC | SBC => FlagLiveness::all()
    ASL | LSR | ROL | ROR | CMP | CPX | CPY => { ..FlagLiveness::all(), overflow: false }
    LDA | LDX | LDY | AND | ORA | EOR | INC | INX | INY | DEC | DEX | DEY |
    PLA | TAX | TAY | TSX | TXA | TYA => {
      { carry: false, zero: true, negative: true, overflow: false }
    }
    CLC | SEC => { carry: true, zero: false, negative: false, overflow: false }
    BIT => { ..FlagLiveness::all(), carry: false }
    CLV => { ..FlagLiveness::none(), overflow: true }
    JSR | PLP => FlagLiveness::all()
    _ => FlagLiveness::none()
  }
}

fn BasicBlock::flags_escape(self: BasicBlock) -> Bool {
  if self.successors.is_empty() {
    return true
  }

  match self.last_non_comment_item() {
    Some({
      item: Inst(
        op=BCC | BCS | BEQ | BMI | BNE | BPL | BVC | BVS,
        mode=Relative(Ident(name="__ret__", ..)) |
          Absolute(Ident(name="__ret__", ..)),
        ..
      ),
    }) => true
    _ => false
  }
}

// Matched call/return demands: a callee is analyzed with the union of what
// its actual callers need at return. No assumption that a call kills every
// flag, and no unconditional "all flags live" at internal RTS instructions.
fn AsmLowering::collect_flag_liveness(self: AsmLowering) -> Unit {
  let returns: Map[String, Array[Int]] = Map([])
  for name in self.targets.subroutines {
    let pending = [self.bb_by_label[name]]
    let seen: Set[Int] = Set([])
    let exits = []
    while !pending.is_empty() {
      let bb = pending.pop().unwrap()
      if seen.contains(bb.id) { continue }
      seen.add(bb.id)
      let yields = bb.items.iter().any(fn(item) {
        match (item, self.program) {
          (Inst(id~, ..), Some(p)) => p.idle.contains(id)
          _ => false
        }
      })
      if bb.flags_escape() || yields { exits.push(bb.id) }
      for link in bb.successors { pending.push(link.bb) }
    }
    returns[name] = exits
  }
  let live_in = self.basic_blocks.map(fn(_) { FlagLiveness::none() })
  let demands = self.basic_blocks.map(fn(_) { FlagLiveness::none() })
  for name in self.targets.subroutines {
    let external = match self.program {
      Some(p) => p.roots.contains(name)
      None => name.has_suffix("__sub")
    }
    if external {
      for id in returns[name] { demands[id] = FlagLiveness::all() }
    }
  }
  let mut changed = true
  while changed {
    changed = false
    let mut index = self.basic_blocks.length() - 1
    while index >= 0 {
      let bb = self.basic_blocks[index]
      let mut live = demands[index]
      for link in bb.successors { live = live.union(live_in[link.bb.id]) }
      let mut i = bb.items.length() - 1
      while i >= 0 {
        match bb.items[i] {
          Inst(id~, op=JSR, mode=Absolute(Ident(name~, ..)), ..) => {
            self.flag_liveness[id] = live
            match returns.get(name) {
              Some(exits) => {
                for exit in exits {
                  let next = demands[exit].union(live)
                  if next != demands[exit] { demands[exit] = next; changed = true }
                }
                live = live_in[self.bb_by_label[name].id]
              }
              None => live = FlagLiveness::all()
            }
          }
          Inst(id~, op~, ..) => {
            self.flag_liveness[id] = live
            live = flag_reads(op).union(live.subtract(flag_writes(op)))
          }
          // The proven inline helper writes C/Z/N but preserves V before
          // transferring to the selected handler. Its emitted setup is exact.
          JumpEngine(_) => live = { ..FlagLiveness::none(), overflow: live.overflow }
          _ => ()
        }
        i -= 1
      }
      if live != live_in[index] { live_in[index] = live; changed = true }
      index -= 1
    }
  }
}

pub fn AsmLowering::flag_live_after(self: AsmLowering, id: Int) -> FlagLiveness {
  match self.flag_liveness.get(id) {
    Some(flags) => flags
    None => FlagLiveness::all()
  }
}

pub fn AsmLowering::is_subroutine_label(self: AsmLowering, label: String) -> Bool {
  self.targets.subroutines.contains(label)
}

// A machine block may participate in several callable entry regions. Follow
// actual edges, not source order; a JSR's callee is not an intraprocedural edge.
// Shared tails stay local until outlining them is proven safe. This also keeps
// backedges in one C frame rather than turning loops into host recursion.
fn AsmLowering::collect_subroutines(self: AsmLowering) -> Unit raise LoweringError {
  let entries = self.targets.subroutines.to_array()
  entries.sort_by(fn(a, b) { self.bb_by_label[a].id - self.bb_by_label[b].id })
  let entry_names: Map[Int, String] = Map([])
  let reach: Map[Int, Set[Int]] = Map([])
  for name in entries {
    let entry = self.bb_by_label[name]
    entry_names[entry.id] = name
    let seen: Set[Int] = Set([])
    let work = [entry]
    while !work.is_empty() {
      let bb = work.pop().unwrap()
      if seen.contains(bb.id) { continue }
      seen.add(bb.id)
      match bb.last_non_comment_item() {
        Some({ item: JumpEngine(_) }) => ()
        _ => for link in bb.successors { work.push(link.bb) }
      }
    }
    reach[entry.id] = seen
  }
  for name in entries {
    let entry = self.bb_by_label.get(name).unwrap_or_error(LoweringError("Missing entry: \{name}"))
    let visited: Set[Int] = Set([])
    let pending = [entry]
    let blocks = []
    let labels: Set[String] = Set([])
    while !pending.is_empty() {
      let bb = pending.pop().unwrap()
      if visited.contains(bb.id) { continue }
      visited.add(bb.id)
      blocks.push(bb)
      for item in bb.items {
        if item is Label(label) { labels.add(label) }
      }
      match bb.last_non_comment_item() {
        Some({ item: JumpEngine(_) }) => ()
        _ => for link in bb.successors {
          // Outline an existing entry only across an acyclic edge. A transfer
          // inside an SCC stays local, even if its target is also callable.
          if entry_names.contains(link.bb.id) && !reach[link.bb.id].contains(bb.id) {
            continue
          }
          pending.push(link.bb)
        }
      }
    }
    blocks.sort_by(fn(a, b) { a.id - b.id })
    self.subroutines.push({ name, blocks, entry, exit: blocks.last().unwrap(), internal_labels: labels })
  }
}

fn  AsmLowering::_cfg_dotfile(self: AsmLowering) -> String {
  let builder = StringBuilder::new()

  builder.write_string("digraph G {\n")

  for bb in self.basic_blocks {
    let label = match bb.name {
      Some(name) => name
      None => bb.id.to_string()
    }

    builder.write_string("  \{bb.id} [label=\"\{label}\"]\n")

    for link in bb.successors {
      builder.write_string("  \{bb.id} -> \{link.bb.id}\n")
    }
  }

  builder.write_string("}\n")

  builder.to_string()
}

fn AsmLowering::recognize_control_flow(self: AsmLowering, sub: AsmSubroutine) -> AsmFlatSubroutine {
  let items = []
  let included: Set[Int] = Set(sub.blocks.map(fn(bb) { bb.id }))
  for bb in sub.blocks {
    for item in bb.items { items.push(item) }
    for link in bb.successors {
      if link.is_fallthrough && !included.contains(link.bb.id) {
        for target in link.bb.items {
          if target is Label(name) && self.targets.subroutines.contains(name) {
            items.push(Inst(id=-1, op=JSR, mode=Absolute(Ident(name~, ty=LabelTy)), comment=Some("fallthrough")))
            items.push(Inst(id=-1, op=RTS, mode=Implied, comment=None))
            break
          }
        }
      }
    }
  }
  if sub.blocks[0].id != sub.entry.id {
    items.insert(0, Inst(id=-1, op=JMP, mode=Absolute(Ident(name=sub.name, ty=LabelTy)), comment=None))
  }
  { name: sub.name, items: self.structure(items) }
}

fn AsmLowering::collect_flat_subroutines(self: AsmLowering) -> Unit {
  for sub in self.subroutines {
    self.flat_subroutines.push(self.recognize_control_flow(sub))
  }
}

// Canonicalize return-only destinations before region recovery. The source
// retains its labels; the internal return marker is never required as input.
fn AsmLowering::canonicalize_returns(self: AsmLowering) -> Unit {
  let returns: Set[String] = Set([])
  let labels = []
  let first: Map[String, AsmItem] = Map([])
  for item in self.lowered {
    match item {
      Label(name) => labels.push(name)
      Comment(_) => ()
      _ => {
        for name in labels { first[name] = item }
        labels.clear()
      }
    }
  }
  let mut changed = true
  while changed {
    changed = false
    for name, item in first {
      let is_return = match item {
        Inst(op=RTS, ..) => true
        Inst(op=JMP, mode=Absolute(Ident(name=target, ..)), ..) => returns.contains(target)
        _ => false
      }
      if is_return && !returns.contains(name) { returns.add(name); changed = true }
    }
  }
  for i in 0..<self.lowered.length() {
    match self.lowered[i] {
      Inst(id~, op~, mode=Relative(Ident(name=target, ..)), comment~) if returns.contains(target) => {
        self.lowered[i] = Inst(id~, op~, mode=Relative(Ident(name="__ret__", ty=LabelTy)), comment~)
      }
      Inst(id~, op=JMP, mode=Absolute(Ident(name=target, ..)), comment~) if returns.contains(target) => {
        self.lowered[i] = Inst(id~, op=RTS, mode=Implied, comment~)
      }
      _ => ()
    }
  }
}

pub fn AsmLowering::lower(self: AsmLowering) -> Unit raise LoweringError {
  self.collect_labels_and_tables()
  self.collect_defines()
  self.collect_instructions()
  self.canonicalize_returns()
  self.collect_basic_blocks()
  self.collect_flag_liveness()
  self.collect_subroutines()
  self.collect_flat_subroutines()

  // @fs.write_string_to_file(path="cfg.dot", content=self.cfg_dotfile())
}
