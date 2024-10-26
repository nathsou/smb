
pub struct AsmLowering {
  items: Array[@parse.SourceItem]
  lowered: Array[AsmItem]
  defines: Map[String, TypedExpr]
  jump_target_labels: Set[String]
  tables: Array[AsmTable]
  tables_by_label: Map[String, AsmTable]
  mut table_offset: Int
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

pub fn lower(self: AsmLowering) -> Unit!LoweringError {
  self.collect_labels_and_tables!()
  self.collect_defines!()
  self.collect_instructions!()
}
