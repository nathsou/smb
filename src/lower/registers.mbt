// Register (A/X/Y) liveness and routine interfaces.
//
// The same matched call/return scheme as the flag analysis: a callee's exits
// are demanded by the union of what its callers read after the call, and a
// call site reads what the callee reads on entry. Together with may-write
// summaries this yields each routine's interface: the registers it consumes
// on entry, and the registers and flags it produces that a caller reads.

pub(all) struct Registers {
  a: Bool
  x: Bool
  y: Bool
} derive (Debug, Eq)

pub fn Registers::none() -> Registers {
  { a: false, x: false, y: false }
}

pub fn Registers::all() -> Registers {
  { a: true, x: true, y: true }
}

pub fn Registers::union(self: Registers, other: Registers) -> Registers {
  { a: self.a || other.a, x: self.x || other.x, y: self.y || other.y }
}

pub fn Registers::subtract(self: Registers, other: Registers) -> Registers {
  { a: self.a && !other.a, x: self.x && !other.x, y: self.y && !other.y }
}

pub fn Registers::intersect(self: Registers, other: Registers) -> Registers {
  { a: self.a && other.a, x: self.x && other.x, y: self.y && other.y }
}

pub fn Registers::is_none(self: Registers) -> Bool {
  !self.a && !self.x && !self.y
}

pub fn Registers::names(self: Registers) -> Array[String] {
  let names = []
  if self.a { names.push("a") }
  if self.x { names.push("x") }
  if self.y { names.push("y") }
  names
}

pub fn FlagLiveness::names(self: FlagLiveness) -> Array[String] {
  let names = []
  if self.carry { names.push("carry") }
  if self.zero { names.push("zero") }
  if self.negative { names.push("negative") }
  if self.overflow { names.push("overflow") }
  names
}

// The index register an addressing mode reads, if any.
pub(all) enum IndexRegister {
  NoIndex
  IndexX
  IndexY
} derive (Debug, Eq)

pub fn AsmAddressingMode::index_register(self: AsmAddressingMode) -> IndexRegister {
  match self {
    ZeroPageX(_) | AbsoluteX(_) | IndexedIndirect(_) => IndexX
    ZeroPageY(_) | AbsoluteY(_) | IndirectIndexed(_) => IndexY
    _ => NoIndex
  }
}

pub fn decoded_index_register(mode: String) -> IndexRegister {
  match mode {
    "zpx" | "absx" | "indx" => IndexX
    "zpy" | "absy" | "indy" => IndexY
    _ => NoIndex
  }
}

fn index_reads(index: IndexRegister) -> Registers {
  match index {
    NoIndex => Registers::none()
    IndexX => { ..Registers::none(), x: true }
    IndexY => { ..Registers::none(), y: true }
  }
}

let reg_a : Registers = { a: true, x: false, y: false }
let reg_x : Registers = { a: false, x: true, y: false }
let reg_y : Registers = { a: false, x: false, y: true }

// Registers an instruction reads. JSR is handled by the interprocedural
// analysis; `accumulator` marks the accumulator addressing mode.
pub fn register_reads(op: @parse.OpCode, accumulator~: Bool, index~: IndexRegister) -> Registers {
  let own = match op {
    STA | ADC | SBC | AND | ORA | EOR | CMP | BIT | PHA | TAX | TAY => reg_a
    ASL | LSR | ROL | ROR => if accumulator { reg_a } else { Registers::none() }
    STX | CPX | INX | DEX | TXA | TXS => reg_x
    STY | CPY | INY | DEY | TYA => reg_y
    _ => Registers::none()
  }
  own.union(index_reads(index))
}

// Registers an instruction writes.
pub fn register_writes(op: @parse.OpCode, accumulator~: Bool) -> Registers {
  match op {
    LDA | ADC | SBC | AND | ORA | EOR | PLA | TXA | TYA => reg_a
    ASL | LSR | ROL | ROR => if accumulator { reg_a } else { Registers::none() }
    LDX | INX | DEX | TAX | TSX => reg_x
    LDY | INY | DEY | TAY => reg_y
    _ => Registers::none()
  }
}

// A compare or BIT consumes its register only for the flags it produces; when
// those are dead (as for the BIT that hides a second entry point in SMB's
// `.db $2c` idiom) the register is not read. Index registers still are.
fn AsmLowering::inst_reads(self: AsmLowering, id: Int, op: @parse.OpCode, mode: AsmAddressingMode) -> Registers {
  let reads = register_reads(op, accumulator=mode is Accumulator, index=mode.index_register())
  let live = self.flag_live_after(id)
  let used = match op {
    BIT => live.zero
    CMP | CPX | CPY => live.carry || live.zero || live.negative
    _ => true
  }
  if used { reads } else { index_reads(mode.index_register()) }
}

fn inst_writes(op: @parse.OpCode, mode: AsmAddressingMode) -> Registers {
  register_writes(op, accumulator=mode is Accumulator)
}

// Exit blocks of every callable entry, following real edges (tail jumps into
// other entries included), plus the idle-yield blocks.
fn AsmLowering::routine_exits(self: AsmLowering) -> Map[String, Array[Int]] {
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
  returns
}

fn AsmLowering::is_root(self: AsmLowering, name: String) -> Bool {
  match self.program {
    Some(p) => p.roots.contains(name)
    None => name.has_suffix("__sub")
  }
}

// Summaries first (they do not depend on caller demands), then liveness. A
// root's exits (RTI, or the idle yield where interrupts are delivered) are
// observed only by the roots themselves, so they demand what roots read.
fn AsmLowering::collect_register_liveness(self: AsmLowering) -> Unit {
  let returns = self.routine_exits()
  let bodies: Map[String, Array[BasicBlock]] = Map([])
  for name, _ in returns {
    let pending = [self.bb_by_label[name]]
    let seen: Set[Int] = Set([])
    let blocks = []
    while pending.pop() is Some(bb) {
      if seen.contains(bb.id) { continue }
      seen.add(bb.id)
      blocks.push(bb)
      for link in bb.successors { pending.push(link.bb) }
    }
    blocks.sort_by(fn(a, b) { a.id - b.id })
    bodies[name] = blocks
  }
  self.collect_routine_writes(bodies)
  let must_writes = self.collect_routine_must_writes(bodies, returns)
  self.collect_routine_reads(bodies, must_writes)
  let mut root_demand = Registers::none()
  for name in self.targets.subroutines {
    if self.is_root(name) { root_demand = root_demand.union(self.routine_inputs(name)) }
  }
  self.collect_live_registers(returns, root_demand)
  self.collect_caller_demands(bodies)
}

// What the callers of each routine read after it returns: the registers and
// flags live after each JSR to it, plus the demands of every routine that
// reaches it by a jump or fallthrough (it then returns to their callers).
// Unlike the demand at exit blocks, this is not widened by other routines
// that happen to share those exits.
fn AsmLowering::collect_caller_demands(self: AsmLowering, bodies: Map[String, Array[BasicBlock]]) -> Unit {
  let entries: Map[Int, String] = Map([])
  for name, _ in bodies {
    entries[self.bb_by_label[name].id] = name
    self.caller_register_demands[name] = Registers::none()
    self.caller_flag_demands[name] = FlagLiveness::none()
  }
  for bb in self.basic_blocks {
    for item in bb.items {
      if item is Inst(id~, op=JSR, mode=Absolute(Ident(name~, ..)), ..) && bodies.contains(name) {
        self.caller_register_demands[name] = self.caller_register_demands[name].union(self.register_live_after(id))
        self.caller_flag_demands[name] = self.caller_flag_demands[name].union(self.flag_live_after(id))
      }
    }
  }
  let mut changed = true
  while changed {
    changed = false
    for name, blocks in bodies {
      for bb in blocks {
        match entries.get(bb.id) {
          Some(other) if other != name => {
            let regs = self.caller_register_demands[other].union(self.caller_register_demands[name])
            let flags = self.caller_flag_demands[other].union(self.caller_flag_demands[name])
            if regs != self.caller_register_demands[other] || flags != self.caller_flag_demands[other] {
              self.caller_register_demands[other] = regs
              self.caller_flag_demands[other] = flags
              changed = true
            }
          }
          _ => ()
        }
      }
    }
  }
}


fn AsmLowering::collect_live_registers(
  self: AsmLowering,
  returns: Map[String, Array[Int]],
  root_demand: Registers,
) -> Unit {
  let live_in = self.basic_blocks.map(fn(_) { Registers::none() })
  let demands = self.basic_blocks.map(fn(_) { Registers::none() })
  for name in self.targets.subroutines {
    if self.is_root(name) {
      for id in returns[name] { demands[id] = root_demand }
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
            self.register_liveness[id] = live
            match returns.get(name) {
              Some(exits) => {
                for exit in exits {
                  let next = demands[exit].union(live)
                  if next != demands[exit] { demands[exit] = next; changed = true }
                }
                live = live_in[self.bb_by_label[name].id]
              }
              None => live = Registers::all()
            }
          }
          Inst(id~, op~, mode~, ..) => {
            self.register_liveness[id] = live
            live = self.inst_reads(id, op, mode).union(live.subtract(inst_writes(op, mode)))
          }
          JumpEngine(site~, ..) => live = self.dispatch_transfer(site, live)
          _ => ()
        }
        i -= 1
      }
      if live != live_in[index] { live_in[index] = live; changed = true }
      index -= 1
    }
  }
  for name, exits in returns {
    let mut demand = Registers::none()
    let mut flags = FlagLiveness::none()
    for exit in exits {
      demand = demand.union(demands[exit])
      flags = flags.union(self.block_flag_demands[exit])
    }
    self.return_register_demands[name] = demand
    // Computed here, after shared tails became entries, so they have one too.
    self.return_flag_demands[name] = flags
  }
}

fn AsmLowering::dispatch_transfer(self: AsmLowering, site: Int, live: Registers) -> Registers {
  let steps = self.program.unwrap().dispatches[site].steps
  let mut live = live
  let mut j = steps.length() - 1
  while j >= 0 {
    let inst = steps[j].inst
    let accumulator = inst.mode == "acc"
    let reads = register_reads(inst.op, accumulator~, index=decoded_index_register(inst.mode))
    live = reads.union(live.subtract(register_writes(inst.op, accumulator~)))
    j -= 1
  }
  live
}

// Registers written on every path from a routine's entry to each of its exits.
fn AsmLowering::collect_routine_must_writes(
  self: AsmLowering,
  bodies: Map[String, Array[BasicBlock]],
  returns: Map[String, Array[Int]],
) -> Map[String, Registers] {
  let summary: Map[String, Registers] = Map([])
  for name, _ in bodies { summary[name] = Registers::all() }
  let mut outer = true
  while outer {
    outer = false
    for name, blocks in bodies {
      let entry = self.bb_by_label[name].id
      let inside: Set[Int] = Set(blocks.map(fn(bb) { bb.id }))
      let out: Map[Int, Registers] = Map([])
      for bb in blocks { out[bb.id] = Registers::all() }
      let mut changed = true
      while changed {
        changed = false
        for bb in blocks {
          let mut written = Registers::all()
          if bb.id == entry {
            written = Registers::none()
          } else {
            for link in bb.predecessors {
              if inside.contains(link.bb.id) { written = written.intersect(out[link.bb.id]) }
            }
          }
          for item in bb.items {
            match item {
              Inst(op=JSR, mode=Absolute(Ident(name=callee, ..)), ..) =>
                written = written.union(summary.get(callee).unwrap_or(Registers::none()))
              Inst(op~, mode~, ..) => written = written.union(inst_writes(op, mode))
              JumpEngine(site~, ..) =>
                for step in self.program.unwrap().dispatches[site].steps {
                  written = written.union(register_writes(step.inst.op, accumulator=step.inst.mode == "acc"))
                }
              _ => ()
            }
          }
          if written != out[bb.id] { out[bb.id] = written; changed = true }
        }
      }
      let mut must = Registers::all()
      for exit in returns[name] {
        if inside.contains(exit) { must = must.intersect(out[exit]) }
      }
      if must != summary[name] { summary[name] = must; outer = true }
    }
  }
  summary
}

// Registers a routine (with its callees) reads before writing them.
fn AsmLowering::collect_routine_reads(
  self: AsmLowering,
  bodies: Map[String, Array[BasicBlock]],
  must_writes: Map[String, Registers],
) -> Unit {
  for name, _ in bodies { self.entry_register_reads[name] = Registers::none() }
  let mut outer = true
  while outer {
    outer = false
    for name, blocks in bodies {
      let live_in: Map[Int, Registers] = Map([])
      for bb in blocks { live_in[bb.id] = Registers::none() }
      let mut changed = true
      while changed {
        changed = false
        let mut index = blocks.length() - 1
        while index >= 0 {
          let bb = blocks[index]
          let mut live = Registers::none()
          for link in bb.successors { live = live.union(live_in.get(link.bb.id).unwrap_or(Registers::none())) }
          let mut i = bb.items.length() - 1
          while i >= 0 {
            match bb.items[i] {
              Inst(op=JSR, mode=Absolute(Ident(name=callee, ..)), ..) => {
                let reads = self.entry_register_reads.get(callee).unwrap_or(Registers::all())
                let kills = must_writes.get(callee).unwrap_or(Registers::none())
                live = reads.union(live.subtract(kills))
              }
              Inst(id~, op~, mode~, ..) => live = self.inst_reads(id, op, mode).union(live.subtract(inst_writes(op, mode)))
              JumpEngine(site~, ..) => live = self.dispatch_transfer(site, live)
              _ => ()
            }
            i -= 1
          }
          if live != live_in[bb.id] { live_in[bb.id] = live; changed = true }
          index -= 1
        }
      }
      let reads = live_in[self.bb_by_label[name].id]
      if reads != self.entry_register_reads[name] { self.entry_register_reads[name] = reads; outer = true }
    }
  }
}

// May-write summaries: registers and flags a routine (with its callees and
// tail-jump targets) can modify before it returns.
fn AsmLowering::collect_routine_writes(self: AsmLowering, bodies: Map[String, Array[BasicBlock]]) -> Unit {
  for name, _ in bodies {
    self.routine_register_writes[name] = Registers::none()
    self.routine_flag_writes[name] = FlagLiveness::none()
  }
  let mut changed = true
  while changed {
    changed = false
    for name, blocks in bodies {
      let mut regs = Registers::none()
      let mut flags = FlagLiveness::none()
      for bb in blocks {
        for item in bb.items {
          match item {
            Inst(op=JSR, mode=Absolute(Ident(name=callee, ..)), ..) => {
              regs = regs.union(self.routine_register_writes.get(callee).unwrap_or(Registers::all()))
              flags = flags.union(self.routine_flag_writes.get(callee).unwrap_or(FlagLiveness::all()))
            }
            Inst(op~, mode~, ..) => {
              regs = regs.union(inst_writes(op, mode))
              flags = flags.union(flag_writes(op))
            }
            JumpEngine(site~, ..) => {
              for step in self.program.unwrap().dispatches[site].steps {
                regs = regs.union(register_writes(step.inst.op, accumulator=step.inst.mode == "acc"))
                flags = flags.union(flag_writes(step.inst.op))
              }
            }
            _ => ()
          }
        }
      }
      if regs != self.routine_register_writes[name] || flags != self.routine_flag_writes[name] {
        self.routine_register_writes[name] = regs
        self.routine_flag_writes[name] = flags
        changed = true
      }
    }
  }
}

pub fn AsmLowering::register_live_after(self: AsmLowering, id: Int) -> Registers {
  self.register_liveness.get(id).unwrap_or(Registers::all())
}

// Registers a routine reads before writing them.
pub fn AsmLowering::routine_inputs(self: AsmLowering, name: String) -> Registers {
  self.entry_register_reads.get(name).unwrap_or(Registers::all())
}

// Registers a routine may modify that some caller reads after it returns.
pub fn AsmLowering::routine_register_outputs(self: AsmLowering, name: String) -> Registers {
  let demand = if self.is_root(name) {
    Registers::none() // roots return only to the idle loop
  } else {
    self.caller_register_demands.get(name).unwrap_or(Registers::all())
  }
  demand.intersect(self.routine_register_writes.get(name).unwrap_or(Registers::all()))
}

// Flags a routine may modify that some caller reads after it returns.
pub fn AsmLowering::routine_flag_outputs(self: AsmLowering, name: String) -> FlagLiveness {
  let demand = if self.is_root(name) {
    FlagLiveness::none()
  } else {
    self.caller_flag_demands.get(name).unwrap_or(FlagLiveness::all())
  }
  demand.intersect(self.routine_flag_writes.get(name).unwrap_or(FlagLiveness::all()))
}
