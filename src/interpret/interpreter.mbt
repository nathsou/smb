
// let const_stack_start: Int = 0x100
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

fn lower_addr_mode(mode: @lower.AsmAddressingMode, label_locs: Map[String, Int]) -> AddressingMode!InterpreterError {
  match mode {
    Accumulator => Accumulator
    Implied => Implied
    Immediate(val) => {
      match val {
        Const(base=_, ~value, size=_) => Immediate(const=value)
        _ => raise InterpreterError("Immediate mode must have a constant value")
      }
    }
    Relative(~addr) => {
      match addr {
        JumpTarget(label) => Relative(target=label_locs[label].or_error!(InterpreterError("Jump target not found: \{label}")))
        _ => raise InterpreterError("Relative mode must have a jump target")
      }
    }
    ZeroPage(~addr) => {
      match addr {
        JumpTarget(label) => ZeroPage(addr=label_locs[label].or_error!(InterpreterError("Jump target not found: \{label}")))
        Const(base=_, ~value, size=_) => ZeroPage(addr=value)
        _  => raise InterpreterError("ZeroPage mode must have a jump target")
      }
    }
    ZeroPageX(~addr) => {
      match addr {
        DataTableOffset(~offset, label=_) => ZeroPageX(data_offset=offset)
        Const(base=_, ~value, size=_) => ZeroPageX(data_offset=value)
        _ => raise InterpreterError("ZeroPageX mode must have a data table offset")
      }
    }
    ZeroPageY(~addr) => {
      match addr {
        DataTableOffset(~offset, label=_) => ZeroPageY(data_offset=offset)
        Const(base=_, ~value, size=_) => ZeroPageY(data_offset=value)
        _ => raise InterpreterError("ZeroPageY mode must have a data table offset")
      }
    }
    Absolute(~addr) => {
      match addr {
        JumpTarget(label) => Absolute(addr=label_locs[label].or_error!(InterpreterError("Jump target not found: \{label}")))
        Const(base=_, ~value, size=_) => Absolute(addr=value)
        DataTableOffset(label=_, ~offset) => Absolute(addr=offset) // one case: cmp SwimTileRepOffset
      }
    }
    AbsoluteX(~addr) => {
      match addr {
        DataTableOffset(~offset, label=_) => AbsoluteX(data_offset=offset)
        Const(base=_, ~value, size=_) => AbsoluteX(data_offset=value)
        _ => raise InterpreterError("AbsoluteX mode must have a data table offset, got: \{addr}")
      }
    }
    AbsoluteY(~addr) => {
      match addr {
        DataTableOffset(~offset, label=_) => AbsoluteY(data_offset=offset)
        Const(base=_, ~value, size=_) => AbsoluteY(data_offset=value)
        _ => raise InterpreterError("AbsoluteY mode must have a data table offset")
      }
    }
    Indirect(_) => raise InterpreterError("Indirect mode not supported")
    IndexedIndirect(~addr) => {
      match addr {
        DataTableOffset(~offset, label=_) => IndexedIndirect(data_offset=offset)
        _ => raise InterpreterError("IndexedIndirect mode must have a data table offset")
      }
    }
    IndirectIndexed(~addr) => {
      match addr {
        DataTableOffset(~offset, label=_) => IndirectIndexed(data_offset=offset)
        Const(base=_, ~value, size=_) => IndirectIndexed(data_offset=value)
        _ => raise InterpreterError("IndirectIndexed mode must have a data table offset")
      }
    }
  }
}

pub fn collect_instructions(lowering: @lower.AsmLowering, label_locs: Map[String, Int]) -> Array[Inst]!InterpreterError {
  let instructions = []
  let mut is_inside_jump_engine = false

  for item in lowering.lowered {
    match item {
      Label(label) => {
        is_inside_jump_engine = label == "JumpEngine"
      }
      Inst(~op, ~mode) => {
        // ignore the JumpEngine subroutine
        if not(is_inside_jump_engine) {
          instructions.push(Inst(~op, mode=lower_addr_mode!(mode, label_locs)))
          let _ = lower_addr_mode!(mode, label_locs)
        }
      }
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
  let insts = collect_instructions!(lowering, label_locs)

  {
    insts,
    data,
    prg_ram: FixedArray::make(0x2000, 0),
    chr_ram: FixedArray::make(0x2000, 0), 
    pc: label_locs["Start"].or_error!(InterpreterError("Start label not found")),
    a: 0,
    x: 0,
    y: 0,
    sp: const_stack_top,
    status: const_default_status_state,
  }
}
