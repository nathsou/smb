
pub struct AsmLowering {
  items: Array[@parse.SourceItem]
  defines: Map[String, LazyVal]
  jump_target_labels: Set[String]
  tables: Array[AsmTable]
  tables_by_label: Map[String, AsmTable]
  mut table_offset: Int
} derive (Show)

pub struct AsmTable {
  name: String
  offset: Int
  data: Array[LazyVal]
  elem_size: @parse.ConstantSize
  comments: Array[String]
} derive (Show)

pub type! AsmLoweringError {
  Unreachable(String)
} derive (Show)

struct LazyVal {
  expr: @parse.SourceExpr
  value: Ref[AsmValue?]
}

pub impl Show for LazyVal with output(self: LazyVal, logger: Logger) -> Unit {
  match self.value.val {
    Some(value) => logger.write_string("\{value}")
    None => logger.write_string("\{self.expr}")
  }
}

pub fn AsmLowering::new(items: Array[@parse.SourceItem]) -> AsmLowering {
  {
    items,
    defines: {},
    tables: [],
    table_offset: 0, // in bytes
    jump_target_labels: Set::new(),
    tables_by_label: {},
  }
}

fn LazyVal::new(expr: @parse.SourceExpr) -> LazyVal {
  { expr, value: Ref::new(None) }
}

enum ConstType {
  Const; DataOffset
}

struct Const {
  base: @parse.ConstantBase
  value: Int
  size: @parse.ConstantSize
  ty: ConstType
}

fn cast_const(const: Const, ty: ConstType) -> AsmValue {
  match ty {
    Const => Const(base=const.base, value=const.value, size=const.size)
    DataOffset => DataTableOffset(offset=const.value, label=None)
  }
}

fn expect_const(value: AsmValue) -> Const!AsmLoweringError {
  match value {
    Const(~base, ~value, ~size) => { base, value, size, ty: Const }
    DataTableOffset(~offset, label=_) => { base: Hexadecimal, value: offset, size: Word, ty: DataOffset }
    _ => raise Unreachable("Expected constant expression, got: \{value}")
  }
}

fn get_common_type(lhs: ConstType, rhs: ConstType) -> ConstType {
  match (lhs, rhs) {
    (Const, Const) => Const
    _ => DataOffset
  }
}

fn eval_lazy_val(self: AsmLowering, val: LazyVal) -> AsmValue!AsmLoweringError {
  match val.value.val {
    Some(value) => value
    None => {
      let v = self.eval_expr!(val.expr)
      val.value.val = Some(v)
      v
    }
  }
}

fn eval_expr(self: AsmLowering, expr: @parse.SourceExpr) -> AsmValue!AsmLoweringError {
  match expr {
    Const(~base, ~value, ~size) => Const(~base, ~value, ~size)
    Ident(name) => {
      match self.defines[name] {
        Some(def) => self.eval_lazy_val!(def)
        None => {
          match self.tables_by_label[name] {
            Some(table) => DataTableOffset(label=Some(name), offset=table.offset)
            None => {
              if self.jump_target_labels.contains(name) {
                JumpTarget(name)
              } else {
                raise Unreachable("Undefined label: \{name}")
              }
            }
          }
        }
      }
    }
    Unary(~op, ~expr) => {
      let v = self.eval_expr!(expr) |> expect_const!()

      match op {
        GetLowByte => {
          if v.size == Byte {
            raise Unreachable("Cannot get low byte of a byte-sized value")
          }

          Const(base=v.base, value=v.value & 0xFF, size=Byte)
        }
        GetHighByte => {
          if v.size == Byte {
            raise Unreachable("Cannot get high byte of a byte-sized value")
          }

          Const(base=v.base, value=(v.value >> 8) & 0xFF, size=Byte)
        }
      }
    }
    Binary(~op, ~lhs, ~rhs) => {
      let lhs = self.eval_expr!(lhs) |> expect_const!()
      let rhs = self.eval_expr!(rhs) |> expect_const!()
      let res_ty = get_common_type(lhs.ty, rhs.ty)

      let res_size: @parse.ConstantSize = match (lhs.size, rhs.size) {
        (Byte, Byte) => Byte
        _ => Word
      }

      let mask = match res_size {
        Byte => 0xFF
        Word => 0xFFFF
      }

      let res: Const = match op {
        Add => { base: Hexadecimal, value: (lhs.value + rhs.value) & mask, size: res_size, ty: res_ty }
        Sub => { base: Hexadecimal, value: (lhs.value - rhs.value) & mask, size: res_size, ty: res_ty }
      }

      cast_const(res, res_ty)
    }
  }
}

fn detect_table(self: AsmLowering, name: String, index: Int) -> AsmTable?!AsmLoweringError {
  let data: Array[LazyVal] = []
  let comments: Array[String] = []
  let mut elem_size: @parse.ConstantSize? = None

  for i in (index + 1)..<self.items.length() {
    match self.items[i] {
      Directive(name="db" | "dw" as name, ~args) => {
        let directive_elem_size: @parse.ConstantSize = if name == "db" { Byte } else { Word }

        match elem_size {
          Some(size) => {
            if size != directive_elem_size {
              raise Unreachable("Inconsistent element size in table")
            }
          }
          None => elem_size = {
            Some(directive_elem_size)
          }
        }

        data.push_iter(args.map(LazyVal::new).iter())
      }
      Comment(~comment, is_inline=_) => comments.push(comment)
      _ => break
    }
  }

  match elem_size {
    Some(elem_size) => {
      Some({ name, offset: self.table_offset, data, elem_size, comments })
    }
    None => None
  }
}

fn collect_labels_and_tables(self: AsmLowering) -> Unit!AsmLoweringError {
  for idx, item in self.items {
    match item {
      Label(name) => {
        let is_alias = match name {
          "GameText" | "AreaAddrOffsets" => true
          _ => false
        }

        match self.detect_table!(name, idx + (if is_alias { 1 } else { 0 })) {
          Some(table) => {
            self.tables.push(table)
            self.table_offset += table.data.length() * table.elem_size.byte_size()
            self.tables_by_label[table.name] = table

            if is_alias {
              self.tables_by_label[name] = table
            }
          }
          None => self.jump_target_labels.insert(name)
        }
      }
      _ => ()
    }
  }
}

fn collect_defines(self: AsmLowering) -> Unit {
  for item in self.items {
    match item {
      Define(~name, ~rhs) => {
        self.defines[name] = LazyVal::new(rhs)
      }
      _ => ()
    }
  }
}

fn eval_defines(self: AsmLowering) -> Unit!AsmLoweringError {
  for val in self.defines.values() {
    let _ = self.eval_lazy_val!(val)
  }
}

fn eval_tables(self: AsmLowering) -> Unit!AsmLoweringError {
  for table in self.tables {
    for lazy_val in table.data {
      let _ = self.eval_lazy_val!(lazy_val)
    }
  }
}

fn collect_instructions(self: AsmLowering) -> Array[AsmItem]!AsmLoweringError {
  let items: Array[AsmItem] = []
  let mut inside_table = false
  let mut item_idx = 0

  while item_idx < self.items.length() {
    let item = self.items[item_idx]
    let mut idx_increment = 1

    match item {
      Label(name) => {
        if not(self.tables_by_label.contains(name)) {
          // this label is a jump target if it's not a table label
          items.push(Label(name))
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
                        raise Unreachable("Unknown label in jump table")
                      }

                      jump_table.push(label)
                    }
                    _ => raise Unreachable("Expected label in jump table")
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

          items.push(JumpEngine(~jump_table))
        } else {
          let addr_mode: AsmAddressingMode = match mode {
            Accumulator => Accumulator
            Implied => Implied
            Immediate(operand) => Immediate(self.eval_expr!(operand))
            Relative(~addr) => Relative(addr=self.eval_expr!(addr))
            AbsoluteOrZeroPage(~addr) | AbsoluteOrZeroPageX(~addr) | AbsoluteOrZeroPageY(~addr) => {
              let addr = self.eval_expr!(addr)
              let size = match addr {
                Const(base=_, value=_, ~size) => size
                JumpTarget(_) | DataTableOffset(_) => Word
              }

              match (mode, size) {
                (AbsoluteOrZeroPage(_), Byte) => ZeroPage(~addr)
                (AbsoluteOrZeroPage(_), Word) => Absolute(~addr)
                (AbsoluteOrZeroPageX(_), Byte) => ZeroPageX(~addr)
                (AbsoluteOrZeroPageX(_), Word) => AbsoluteX(~addr)
                (AbsoluteOrZeroPageY(_), Byte) => ZeroPageY(~addr)
                (AbsoluteOrZeroPageY(_), Word) => AbsoluteY(~addr)
                _ => raise Unreachable("Invalid addressing mode")
              }
            }
            Indirect(~addr) => Indirect(addr=self.eval_expr!(addr))
            IndexedIndirect(~addr) => IndexedIndirect(addr=self.eval_expr!(addr))
            IndirectIndexed(~addr) => IndirectIndexed(addr=self.eval_expr!(addr))
          }

          items.push(Inst(~op, mode=addr_mode))
        }
      }
      Comment(~comment, ~is_inline) => {
        if not(inside_table) {
          items.push(Comment(~comment, ~is_inline))
        }
      }
      Directive(name="db" | "dw" as name, ~args) => {
        if not(inside_table) {
          raise Unreachable("Raw data in code detected: \{name} \{args}")
        }
      }
      _ => ()
    }

    item_idx += idx_increment
  }

  items
}

pub fn lower(self: AsmLowering) -> Array[AsmItem]!AsmLoweringError {
  self.collect_labels_and_tables!()
  self.collect_defines()
  self.eval_defines!()
  self.eval_tables!()
  self.collect_instructions!()
}
