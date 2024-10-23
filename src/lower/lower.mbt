
pub struct AsmLowering {
  items: Array[@parse.SourceItem]
  defines: Map[String, LazyVal]
  tables: Array[AsmTable]
  subroutine_labels: Set[String]
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
    table_offset: 0,
    subroutine_labels: Set::new(),
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
              if self.subroutine_labels.contains(name) {
                SubroutineLabel(name)
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
      Comment(comment) => comments.push(comment)
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
        match name {
          "GameText" | "AreaAddrOffsets" => {
            // aliases for the next table label
            let table = self.detect_table!(name, idx + 1).unwrap()
            self.tables.push(table)
            self.tables_by_label[name] = table
            self.tables_by_label[table.name] = table
          }
          _ => {
            match self.detect_table!(name, idx) {
              Some(table) => {
                self.tables.push(table)
                self.table_offset += table.data.length() * table.elem_size.byte_size()
                self.tables_by_label[name] = table
              }
              None => self.subroutine_labels.insert(name)
            }
          }
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

pub fn lower(self: AsmLowering) -> Unit!AsmLoweringError {
  self.collect_labels_and_tables!()
  self.collect_defines()
  self.eval_defines!()
  self.eval_tables!()
}
