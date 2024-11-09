
pub struct AsmLowering {
  items: Array[@parse.SourceItem]
  lowered: Array[AsmItem]
  defines: Map[String, TypedExpr]
  jump_target_labels: Set[String]
  tables: Array[AsmTable]
  tables_by_label: Map[String, AsmTable]
  mut table_offset: Int
  targets: JumpTargets
  basic_blocks: Array[BasicBlock]
  bb_by_label: Map[String, BasicBlock]
  subroutines: Array[AsmSubroutine]
  self_contained_subroutine_labels: Set[String]
} derive (Show)

pub type! LoweringError String derive(Show)

pub fn AsmLowering::new(items: Array[@parse.SourceItem]) -> AsmLowering {
  {
    items,
    lowered: [],
    defines: {},
    tables: [],
    table_offset: 0, // in bytes
    jump_target_labels: Set::new(),
    tables_by_label: {},
    targets: { jump_targets: Set::new(), subroutines: Set::new(), externals: Set::new() },
    basic_blocks: [],
    bb_by_label: {},
    subroutines: [],
    self_contained_subroutine_labels: Set::new(),
  }
}

fn detect_table(self: AsmLowering, name: String, index: Int) -> AsmTable?!LoweringError {
  let lines: Array[AsmTableLine] = []
  let mut elem_size: @parse.ConstantSize? = None
  let mut length = 0
  let mut i = index + 1

  while i < self.items.length() {
    match self.items[i] {
      Directive(name="db" | "dw" as name, ~args) => {
        let directive_elem_size: @parse.ConstantSize = if name == "db" { Byte } else { Word }

        match elem_size {
          Some(size) => {
            if size != directive_elem_size {
              raise LoweringError("Inconsistent element size in table")
            }
          }
          None => elem_size = {
            Some(directive_elem_size)
          }
        }

        length += args.length()

        let data = []

        for arg in args {
          data.push(self.type_expr!(arg))
        }

        lines.push(Data(~data, comment=Ref::new("")))
      }
      Comment(~comment, ~is_inline) => {
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

fn collect_labels_and_tables(self: AsmLowering) -> Unit!LoweringError {
  let mut idx = 0

  while idx < self.items.length() {
    let item = self.items[idx]
    let mut idx_increment = 1

    match item {
      Label(name) => {
        let is_alias = match name {
          "GameText" | "AreaAddrOffsets" => true
          _ => false
        }

        match self.detect_table!(name, idx + (if is_alias { 1 } else { 0 })) {
          Some(table) => {
            self.tables.push(table)
            self.table_offset += table.size
            self.tables_by_label[table.name] = table

            if is_alias {
              let alias = match self.items[idx + 1] {
                Label(name) => name
                _ => raise LoweringError("Expected alias label")
              }

              self.tables_by_label[alias] = table
            }

            idx_increment = table.items_count
          }
          None => {
            self.jump_target_labels.insert(name)
          }
        }
      }
      _ => ()
    }

    idx += idx_increment
  }
}

fn type_expr(self: AsmLowering, expr: @parse.SourceExpr) -> TypedExpr!LoweringError {
  match expr {
    Const(~base, ~value, ~size) => {
      let ty = match size {
        Byte => ConstTy(Byte)
        Word => ConstTy(Word)
      }

      Const(~base, ~value, ~size, ~ty)
    }
    Ident(name) => {
      match self.tables_by_label[name] {
        Some(table) => Ident(~name, ty=ArrayTy(table.elem_size))
        None => match self.defines[name] {
          Some(expr) => Ident(~name, ty=expr.ty())
          None => Ident(~name, ty=LabelTy)
        }
      }
    }
    Unary(~op, ~expr) => {
      let expr = self.type_expr!(expr)
      Unary(~op, ~expr, ty=expr.ty())
    }
    Binary(~op, ~lhs, ~rhs) => {
      let lhs = self.type_expr!(lhs)
      let rhs = self.type_expr!(rhs)
      Binary(~op, ~lhs, ~rhs, ty=lhs.ty())
    }
  }
}

fn collect_defines(self: AsmLowering) -> Unit!LoweringError {
  for item in self.items {
    match item {
      Define(~name, ~rhs) => {
        self.defines[name] = self.type_expr!(rhs)
      }
      _ => ()
    }
  }
}

fn collect_instructions(self: AsmLowering) -> Unit!LoweringError {
  let mut inside_table = false
  let mut item_idx = 0

  while item_idx < self.items.length() {
    let item = self.items[item_idx]
    let mut idx_increment = 1

    match item {
      Label(name) => {
        if not(self.tables_by_label.contains(name)) {
          // this label is a jump target if it's not a table label
          
          self.lowered.push(Label(name))
          inside_table = false
        } else {
          inside_table = true
        }
      }
      Inst(~op, ~mode) => {
        if op == JSR && (match mode { AbsoluteOrZeroPage(addr=Ident("JumpEngine")) => true; _ => false }) {
          let jump_table: Array[String] = []
          inside_table = true

          for i in (item_idx + 1)..<self.items.length() {
            match self.items[i] {
              Directive(name="dw", ~args) => {
                for arg in args {
                  match arg {
                    Ident(label) => {
                      if not(self.jump_target_labels.contains(label)) {
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

          self.lowered.push(JumpEngine(~jump_table))
        } else {
          let addr_mode: AsmAddressingMode = match mode {
            Accumulator => Accumulator
            Implied => Implied
            Immediate(operand) => Immediate(self.type_expr!(operand))
            Relative(~addr) => Relative(self.type_expr!(addr))
            AbsoluteOrZeroPage(~addr) | AbsoluteOrZeroPageX(~addr) | AbsoluteOrZeroPageY(~addr) => {
              let addr = self.type_expr!(addr)
              let size: @parse.ConstantSize = match addr.ty() {
                ConstTy(size) => size
                ArrayTy(_) | LabelTy => Word
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
            Indirect(~addr) => Indirect(self.type_expr!(addr))
            IndexedIndirect(~addr) => IndexedIndirect(self.type_expr!(addr))
            IndirectIndexed(~addr) => IndirectIndexed(self.type_expr!(addr))
          }

          let comment = match self.items[item_idx + 1] {
            Comment(~comment, is_inline=true) => Some(comment)
            _ => None
          }

          self.lowered.push(Inst(~op, mode=addr_mode, ~comment))
        }
      }
      Comment(~comment, is_inline=false) => {
        if not(inside_table) {
          self.lowered.push(Comment(comment))
        }
      }
      Directive(name="db" | "dw" as name, ~args) => {
        if not(inside_table) {
          raise LoweringError("Raw data in code detected: \{name} \{args}")
        }
      }
      _ => ()
    }

    item_idx += idx_increment
  }
}

struct JumpTargets {
  jump_targets: Set[String]
  subroutines: Set[String]
  externals: Set[String]
} derive (Show)

fn collect_jump_targets(self: AsmLowering) -> Unit {
  for item in self.lowered {
    match item {
      Inst(
        op=JMP | JSR | BCC | BCS | BEQ | BMI | BNE | BPL | BVC | BVS as op,
        mode=Relative(Ident(name=label, ..)) | Absolute(Ident(name=label, ..)),
        ..
      ) => {
        if op == JSR {
          self.targets.subroutines.insert(label)
        } else {
          self.targets.jump_targets.insert(label)
        }
      }
      JumpEngine(~jump_table) => {
        for label in jump_table {
          self.targets.subroutines.insert(label)
        }
      }
      _ => ()
    }
  }
}

fn contains(self: JumpTargets, label: String) -> Bool {
  self.jump_targets.contains(label) || self.subroutines.contains(label)
}

fn collect_basic_block_leaders(self: AsmLowering) -> Set[Int] {
  self.collect_jump_targets()
  let leader_indices = Set::new()

  // the first non-comment instruction is always a leader
  let first_inst_idx = self.lowered.search_by(fn(item) {
    match item {
      Comment(_) => false
      _ => true
    }
  })

  leader_indices.insert(first_inst_idx.unwrap())
  
  for i in 0..<self.lowered.length() {
    match self.lowered[i] {
      // the target of a jump instruction is a leader
      Label(label) => {
        if self.targets.contains(label) {
          leader_indices.insert(i)
        }
      }
      Inst(~op, ..) => {
        if op.is_branching() {
          // the next instruction after a branching instruction is a leader
          leader_indices.insert(i + 1)
        }
      }
      JumpEngine(_) => leader_indices.insert(i + 1)
      _ => ()
    }
  }

  leader_indices
}

fn collect_basic_blocks(self: AsmLowering) -> Unit!LoweringError {
  let leader_indices = self.collect_basic_block_leaders()

  // Starting from a leader, the set of all following instructions until and not including the next leader is the basic block corresponding to the starting leader.
  for leader in leader_indices {
    let bb = {
      id: self.basic_blocks.length(),
      name: None,
      items: [],
      predecessors: [],
      successors: [],
      is_subroutine_entry: false,
      subroutine: None,
    }

    for i in leader..<self.lowered.length() {
      match self.lowered[i] {
        Label(label) => {
          bb.name = Some(label)
          self.bb_by_label[label] = bb
        }
        _ => ()
      }

      bb.items.push(self.lowered[i])

      if leader_indices.contains(i + 1) {
        break
      }
    }

    self.basic_blocks.push(bb)
  }

  fn connect(a: BasicBlock, b: BasicBlock) -> Unit {
    a.successors.push(b)
    b.predecessors.push(a)
  }

  // connect basic blocks
  for bb in self.basic_blocks {
    let last_item = bb.items.last().unwrap()

    match last_item {
      Inst(
        op=JMP | BCC | BCS | BEQ | BMI | BNE | BPL | BVC | BVS,
        mode=Relative(Ident(name=label, ..)) | Absolute(Ident(name=label, ..)),
        ..
      ) => {
        match self.bb_by_label[label] {
          Some(target_bb) => {
            connect(bb, target_bb)
          }
          None => raise LoweringError("Unknown jump target: \{label}")
        }
      }
      JumpEngine(~jump_table) => {
        for label in jump_table {
          match self.bb_by_label[label] {
            Some(target_bb) => {
              connect(bb, target_bb)
            }
            None => raise LoweringError("Unknown jump target: \{label}")
          }
        }
      }
      _ => ()
    }

    // check for fallthrough to the next basic block
    match bb.items.last() {
      Some(Inst(~op, ..)) => {
        match op {
          RTI | RTS | BRK | JMP => ()
          _ => {
            if bb.id + 1 < self.basic_blocks.length() {
              connect(bb, self.basic_blocks[bb.id + 1])
            }
          }
        }
      }
      _ => ()
    }
  }
}

fn collect_labels(self: BasicBlock, labels: Set[String]) -> Unit {
  for item in self.items {
    match item {
      Label(name) => labels.insert(name)
      _ => ()
    }
  }
}

fn collect_external_labels(self: BasicBlock, internal_labels: Set[String], external_labels: Set[String]) -> Unit {
    for item in self.items {
      match item {
      Inst(
        op=JSR | JMP | BCC | BCS | BEQ | BMI | BNE | BPL | BVC | BVS,
        mode=Relative(Ident(name=label, ..)) | Absolute(Ident(name=label, ..)),
        ..
      ) => {
        if not(internal_labels.contains(label)) {
          external_labels.insert(label)
        }
      }
      JumpEngine(~jump_table) => {
        for label in jump_table {
          if not(internal_labels.contains(label)) {
            external_labels.insert(label)
          }
        }
      }
        _ => ()
      }
  }
}

fn collect_external_labels(self: AsmSubroutine, targets: JumpTargets) -> Unit {
  for bb in self.blocks {
      bb.collect_external_labels(self.internal_labels, self.external_jumps)

      for label in self.external_jumps {
        targets.externals.insert(label)
      }
  }
}

fn collect_subroutines(self: AsmLowering) -> Unit {
  let mut idx = 0

  while idx < self.basic_blocks.length() {
    let bb = self.basic_blocks[idx]

    match bb.name {
      Some(name) => {
        if self.targets.subroutines.contains(name) {
          let sub = {
            name,
            blocks: [bb],
            entry: bb,
            exit: bb,
            internal_labels: Set::new(),
            external_jumps: Set::new(),
            is_self_contained: false,
          }

          let mut exit_bb = bb
          bb.is_subroutine_entry = true
          bb.subroutine = Some(sub)

          for i in (idx + 1)..<self.basic_blocks.length() {
            let bb = self.basic_blocks[i]
            bb.subroutine = Some(sub)
            sub.blocks.push(bb)
            bb.collect_labels(sub.internal_labels)

            if bb.successors.length() == 0 {
              exit_bb = bb
              break
            }
          }

          sub.collect_external_labels(self.targets)

          self.subroutines.push(sub)
          idx = exit_bb.id
        } else {
          idx += 1
        }
      }
      _ => {
        idx += 1
      }
    }
  }
}

// a subroutine is considered self-contained if it doesn't call any external labels
// and if it doesn't contain any labels that are called from outside
fn is_self_contained(self: AsmSubroutine, targets: JumpTargets) -> Bool {
  if self.external_jumps.size() > 0 {
    return false
  }

  for label in self.internal_labels {
    if targets.externals.contains(label) {
      return false
    }
  }

  true
}

// as a first step, only convert self-contained subroutines into functions
fn collect_self_contained_subroutines(self: AsmLowering) -> Unit {
  let global_labels = Set::new()

  for bb in self.basic_blocks {
    if bb.subroutine.is_empty() {
      bb.collect_labels(global_labels)
      bb.collect_external_labels(global_labels, self.targets.externals)
    }
  }

  for sub in self.subroutines {
    if sub.is_self_contained(self.targets) {
      self.self_contained_subroutine_labels.insert(sub.name)
      sub.is_self_contained = true

      // insert a jump instruction in place of automatic fallthroughs to subroutines
      for pred in sub.entry.predecessors {
        pred.items.push(Inst(op=JMP, mode=Absolute(Ident(name=sub.name, ty=LabelTy)), comment=Some("<gen: fallthrough>")))
      }
    }
  }
}

fn _build_cfg_dotfile(self: AsmLowering) -> String {
  let builder = StringBuilder::new()

  builder.write_string("digraph G {\n")

  for bb in self.basic_blocks {
    let name = match bb.name {
      Some(name) => name
      None => bb.id.to_string()
    }

    builder.write_string("\t\{name} [label=\"\{name}\"]\n")

    for succ in bb.successors {
      let succ_name = match succ.name {
        Some(name) => name
        None => succ.id.to_string()
      }

      builder.write_string("\t\{name} -> \{succ_name}\n")
    }
  }

  builder.write_string("}\n")

  builder.to_string()
}

fn _show_bbs(self: AsmLowering) -> Unit {
  for bb in self.basic_blocks {
    println("Basic block \{bb.id} (\{bb.name.or_default()})")
    for item in bb.items {
      println("\{item}")
    }
    println("")
  }
}

pub fn lower(self: AsmLowering) -> Unit!LoweringError {
  self.collect_labels_and_tables!()
  self.collect_defines!()
  self.collect_instructions!()
  self.collect_basic_blocks!()
  self.collect_subroutines()
  self.collect_self_contained_subroutines()
}
