
let const_reset_vector: Int = 0xfffc
let const_stack_start: Int = 0x100
let const_stack_top: Int = 0xfd
let const_default_status_state: Int = 0b00100100

pub struct Interpreter {
  insts: Array[Inst]
  data: FixedArray[Byte]
  prg_ram: FixedArray[Byte]
  chr_ram: FixedArray[Byte]
  pc: Int
  a: Int
  x: Int
  y: Int
  sp: Int
  status: Int
}

// status register
// 7  bit  0
// ---- ----
// NVss DIZC
// |||| ||||
// |||| |||+- Carry
// |||| ||+-- Zero
// |||| |+--- Interrupt Disable
// |||| +---- Decimal
// ||++------ No CPU effect, see: the B flag
// |+-------- Overflow
// +--------- Negative

pub type! InterpreterError String derive (Show)

fn collect_label_locs(items: Array[@lower.AsmItem]) -> Map[String, Int] {
  let label_locs = {}
  let mut instructions = 0

  for item in items {
    match item {
      Label(label) => {
        label_locs[label] = instructions
      }
      Inst(_) | JumpEngine(_) => {
        instructions += 1
      }
      _ => ()
    }
  }

  label_locs
}

fn collect_tables(lowering: @lower.AsmLowering) -> FixedArray[Byte]!InterpreterError {
  let length = lowering.table_offset
  let data = FixedArray::make(length, (0).to_byte())

  let mut offset = 0

  for table in lowering.tables {
    for elem in table.data {
      let value = match elem.unwrap() {
        Const(~value, size=_, base=_) => value
        DataTableOffset(~offset, label=_) => offset
        JumpTarget(label) => raise InterpreterError("JumpTarget used inside data section: \{label}")
      }

      match table.elem_size {
        Byte => {
          data[offset] = value.to_byte()
          offset += 1
        }
        Word => {
          // little endian
          data[offset] = value.to_byte()
          data[offset + 1] = (value >> 8).to_byte()
          offset += 2
        }
      }
    }
  }

  data
}

pub fn collect_instructions(lowering: @lower.AsmLowering, label_locs: Map[String, Int]) -> Array[Inst] {
  let instructions = []

  for item in lowering.lowered {
    match item {
      Inst(~op, ~mode) => instructions.push(Inst(~op, ~mode))
      JumpEngine(~jump_table) => {
        let table: Array[Int] = []
        
        for label in jump_table {
          table.push(label_locs[label].unwrap())
        }

        instructions.push(JumpEngine(jump_table=table))
      }
      _ => ()
    }
  }

  instructions
}

pub fn Interpreter::from(lowering: @lower.AsmLowering) -> Interpreter!InterpreterError {
  let label_locs = collect_label_locs(lowering.lowered)
  let data = collect_tables!(lowering)
  let insts = collect_instructions(lowering, label_locs)

  {
    insts,
    data,
    prg_ram: FixedArray::make(0x2000, 0),
    chr_ram: FixedArray::make(0x2000, 0), 
    pc: label_locs["Start"].unwrap(),
    a: 0,
    x: 0,
    y: 0,
    sp: const_stack_top,
    status: const_default_status_state,
  }
}
