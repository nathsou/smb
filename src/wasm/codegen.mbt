// WebAssembly backend.
//
// The generated out holds the game's routines. It shares one memory with
// a runtime out compiled from the C runtime (video, audio, the native
// display routines), and imports:
//
// - env.memory;
// - the addresses of the runtime's machine state as i32 globals: `ram`
//   (2 KiB), `data` (32 KiB of PRG ROM), and one byte per register and flag
//   under the C names (`a`, `carry_flag`, ...);
// - the runtime functions it calls: bus access, APU and joypad writes, the
//   idle yield and trap, and the native routines.
//
// It exports every routine, and `cpu_reset_entry` / `cpu_nmi_entry`.
//
// Control flow is structured with the algorithm of Ramsey, "Beyond
// Relooper" (ICFP 2022): on a reducible CFG, each block is emitted once under
// its immediate dominator; merge nodes (several forward predecessors) follow
// a `block` that forward branches leave, loop headers open a `loop` that back
// edges continue. A function whose CFG is irreducible runs as a loop over a
// `br_table` on a block-number local instead.

pub suberror WasmError { WasmError(String) } derive(Debug)

priv enum Ctx {
  IfThenElse
  LoopHeadedBy(Int)
  BlockFollowedBy(Int)
}

priv struct Imports {
  globals : Array[String]
  global_index : Map[String, Int]
  funcs : Array[(String, Int)] // name, type
  func_index : Map[String, Int]
}

// Function types, by index.
let types : Array[(Array[Int], Array[Int])] = [
  ([], []), // 0: routines
  ([I32], []), // 1
  ([I32, I32], []), // 2
  ([I32], [I32]), // 3: helpers, read_byte
]

fn runtime_type(name : String) -> Int {
  match name {
    "read_byte" => 3
    "dynamic_ram_write" | "apu_write" | "ppu_bus_write" => 2
    "write_joypad1" | "write_joypad2" | "cpu_yield" | "cpu_unresolved_jump" | "ppu_bus_oam_dma" => 1
    _ => 0
  }
}

priv struct Gen {
  imports : Imports
  defined : Map[String, Int] // function index of generated functions
  f : @ir.Function
  code : Buf
  scratch : Int // an extra local
  state : Int // block-number local for the dispatch loop
  // Machine variables in locals (see `emit`): the first local of the ten,
  // or -1 to access them in memory.
  vars : Int
  layout : Map[String, Int]
  info : @ir.Analysis
  block_in : Array[Int]
  assigned : Int
  read : Int
}

fn var_slot(v : @ir.Var) -> Int {
  match v {
    A => 0; X => 1; Y => 2; SP => 3
    C => 4; Z => 5; N => 6; V => 7; I => 8; D => 9
  }
}

// Writes back the variables in `mask` that this function changed.
fn Gen::spill(self : Gen, mask : Int) -> Unit raise WasmError {
  if self.vars < 0 { return }
  for v in @ir.vars_of(mask & self.assigned) {
    let offset = self.base(v.c_name(), None)
    self.local_get(self.vars + var_slot(v))
    self.store8(offset)
  }
}

// Loads the variables in `mask` this function reads or changes.
fn Gen::reload(self : Gen, mask : Int) -> Unit raise WasmError {
  if self.vars < 0 { return }
  for v in @ir.vars_of(mask & (self.read | self.assigned)) {
    let offset = self.base(v.c_name(), None)
    self.load8(offset)
    self.local_set(self.vars + var_slot(v))
  }
}

fn Gen::live_in(self : Gen, name : String) -> Int {
  self.info.live_in.get(name).unwrap_or(1023)
}

fn Gen::op(self : Gen, op : Int) -> Unit {
  self.code.byte(op)
}

fn Gen::const_(self : Gen, v : Int) -> Unit {
  self.code.byte(I32_CONST)
  self.code.i32(v)
}

fn Gen::global(self : Gen, name : String) -> Unit {
  self.code.byte(GLOBAL_GET)
  self.code.u32(self.imports.global_index[name])
}

fn Gen::call(self : Gen, name : String) -> Unit raise WasmError {
  let index = match self.defined.get(name) {
    Some(i) => i
    None =>
      match self.imports.func_index.get(name) {
        Some(i) => i
        None => raise WasmError("Unknown function: \{name}")
      }
  }
  self.code.byte(CALL)
  self.code.u32(index)
}

fn Gen::load8(self : Gen, offset : Int) -> Unit {
  self.code.byte(I32_LOAD8_U)
  self.code.u32(0) // alignment
  self.code.u32(offset)
}

fn Gen::store8(self : Gen, offset : Int) -> Unit {
  self.code.byte(I32_STORE8)
  self.code.u32(0)
  self.code.u32(offset)
}

// Splits a constant part off an address, for the memory offset.
fn split_offset(addr : @ir.Expr) -> (@ir.Expr?, Int) {
  match addr.value() {
    Some(v) => (None, v)
    None =>
      match addr {
        Binary(Add, l, r) =>
          match (l.value(), r.value()) {
            (Some(v), _) if v >= 0 => (Some(r), v)
            (_, Some(v)) if v >= 0 => (Some(l), v)
            _ => (Some(addr), 0)
          }
        _ => (Some(addr), 0)
      }
  }
}

// Pushes the address of the runtime object `name` plus `index`; returns the
// memory offset to add. With a layout the object's address is a constant
// and goes into the offset.
fn Gen::base(self : Gen, name : String, index : @ir.Expr?) -> Int raise WasmError {
  match self.layout.get(name) {
    Some(address) => {
      match index {
        Some(e) => self.expr(e)
        None => self.const_(0)
      }
      address
    }
    None => {
      self.global(name)
      if index is Some(e) {
        self.expr(e)
        self.op(I32_ADD)
      }
      0
    }
  }
}

// Pushes the address of a RAM byte; returns the memory offset to use.
fn Gen::ram_address(self : Gen, addr : @ir.Expr) -> Int raise WasmError {
  let (rest, offset) = split_offset(addr)
  self.base("ram", rest) + offset
}

fn Gen::rom_address(self : Gen, addr : @ir.Expr) -> Int raise WasmError {
  let r = addr.range(self.f.temps)
  let (rest, offset) = split_offset(addr)
  if r.lo >= 0x8000 && r.hi <= 0xffff && offset >= 0x8000 {
    self.base("data", rest) + offset - 0x8000
  } else {
    // like the C build's ROM(): (address - $8000) modulo 32 KiB; a 16 KiB
    // image is mirrored in the buffer
    self.base("data", Some(Binary(And, addr, Const(0x7fff))))
  }
}

fn Gen::masked(self : Gen, e : @ir.Expr, hi : Int) -> Unit raise WasmError {
  self.expr(e)
  let r = e.range(self.f.temps)
  if r.lo < 0 || r.hi > hi {
    self.const_(hi)
    self.op(I32_AND)
  }
}

fn Gen::expr(self : Gen, e : @ir.Expr) -> Unit raise WasmError {
  match e {
    Const(v) | Sym(_, v) => self.const_(v)
    Var(v) =>
      if self.vars >= 0 {
        self.local_get(self.vars + var_slot(v))
      } else {
        let offset = self.base(v.c_name(), None)
        self.load8(offset)
      }
    Temp(-1) => self.local_get(self.scratch) // the scratch local
    Temp(t) => {
      self.code.byte(LOCAL_GET)
      self.code.u32(t + (if self.f.helper { 1 } else { 0 }))
    }
    Param => {
      self.code.byte(LOCAL_GET)
      self.code.u32(0)
    }
    Load(Ram, a) => {
      let offset = self.ram_address(a)
      self.load8(offset)
    }
    Load(Rom, a) => {
      let offset = self.rom_address(a)
      self.load8(offset)
    }
    Load(Bus, a) => {
      // ROM and RAM directly (pointers mostly read ROM tables); the
      // runtime's read_byte for everything else
      self.masked(a, 0xffff)
      self.code.byte(LOCAL_TEE)
      self.code.u32(self.scratch)
      self.const_(0x8000)
      self.op(I32_GE_S)
      self.op(IF)
      self.code.byte(I32)
      let rom = self.base("data", Some(Binary(And, Temp(-1), Const(0x7fff))))
      self.load8(rom)
      self.op(ELSE)
      self.local_get(self.scratch)
      self.const_(0x2000)
      self.op(I32_LT_S)
      self.op(IF)
      self.code.byte(I32)
      let ram = self.base("ram", Some(Binary(And, Temp(-1), Const(0x7ff))))
      self.load8(ram)
      self.op(ELSE)
      self.local_get(self.scratch)
      self.call("read_byte")
      self.op(END)
      self.op(END)
    }
    Unary(Not, a) => {
      self.expr(a)
      self.op(I32_EQZ)
    }
    Unary(Lo, a) | Unary(Byte, a) => self.masked(a, 0xff)
    Unary(Hi, a) => {
      self.expr(a)
      self.const_(8)
      self.op(I32_SHR_U)
      self.const_(0xff)
      self.op(I32_AND)
    }
    Binary(op, l, r) => {
      self.expr(l)
      self.expr(r)
      self.op(
        match op {
          Add => I32_ADD
          Sub => I32_SUB
          And => I32_AND
          Or => I32_OR
          Xor => I32_XOR
          Shl => I32_SHL
          Shr => I32_SHR_S
          Eq => I32_EQ
          Ne => I32_NE
          Lt => I32_LT_S
          Ge => I32_GE_S
        },
      )
    }
  }
}

fn Gen::local_set(self : Gen, index : Int) -> Unit {
  self.code.byte(LOCAL_SET)
  self.code.u32(index)
}

fn Gen::local_get(self : Gen, index : Int) -> Unit {
  self.code.byte(LOCAL_GET)
  self.code.u32(index)
}

fn Gen::stmt(self : Gen, s : @ir.Stmt) -> Unit raise WasmError {
  match s {
    Assign(v, e) =>
      if self.vars >= 0 {
        self.expr(v.width().wrap(e, self.f.temps))
        self.local_set(self.vars + var_slot(v))
      } else {
        let offset = self.base(v.c_name(), None)
        self.expr(e)
        self.store8(offset)
      }
    Let(t, e) => {
      let hi = match self.f.temps[t].width {
        U8 => 0xff
        U16 => 0xffff
        _ => -1
      }
      if hi < 0 { self.expr(e) } else { self.masked(e, hi) }
      self.local_set(t + (if self.f.helper { 1 } else { 0 }))
    }
    Store(Ram, a, v) => {
      let offset = self.ram_address(a)
      self.expr(v)
      self.store8(offset)
    }
    Store(_, a, v) => {
      self.masked(a, 0xffff)
      self.masked(v, 0xff)
      self.call("dynamic_ram_write")
    }
    Effect("write_joypads", args) => {
      self.masked(args[0], 0xff)
      self.code.byte(LOCAL_TEE)
      self.code.u32(self.scratch)
      self.call("write_joypad1")
      self.local_get(self.scratch)
      self.call("write_joypad2")
    }
    Effect("apu_write", args) => {
      self.masked(args[0], 0xffff)
      self.masked(args[1], 0xff)
      self.call("apu_write")
    }
    Effect("ppu_bus_write", args) => {
      self.expr(args[0])
      self.masked(args[1], 0xff)
      self.call("ppu_bus_write")
    }
    Effect(name, args) => {
      for a in args { self.masked(a, 0xff) }
      self.call(name)
    }
    Eval(e) => {
      self.expr(e)
      self.op(DROP)
    }
    Call(name) => {
      self.spill(self.live_in(name))
      self.call(name)
      self.reload(self.info.may_write.get(name).unwrap_or(1023))
    }
  }
}

// Terminators that leave the function.
fn Gen::exit(self : Gen, term : @ir.Term) -> Unit raise WasmError {
  // what the caller, the callee or the idle loop reads from here
  match term {
    Dispatch(helper, _, cases) => {
      let mut mask = self.live_in(helper)
      for c in cases { mask = mask | self.live_in(c.1) }
      self.spill(mask)
    }
    _ => self.spill(self.info.term_live(self.f, term, self.block_in))
  }
  match term {
    Return => self.op(RETURN)
    ReturnValue(e) => {
      self.masked(e, 0xffff)
      self.op(RETURN)
    }
    TailCall(g) => {
      self.call(g)
      self.op(RETURN)
    }
    Yield(pc) => {
      self.const_(pc)
      self.call("cpu_yield")
      self.op(RETURN)
    }
    Trap(pc) => {
      self.const_(pc)
      self.call("cpu_unresolved_jump")
      self.op(UNREACHABLE)
    }
    Dispatch(helper, return_address, cases) => {
      self.const_(return_address)
      self.call(helper)
      self.local_set(self.scratch)
      for case in cases {
        self.local_get(self.scratch)
        self.const_(case.0)
        self.op(I32_EQ)
        self.op(IF)
        self.code.byte(VOID)
        self.call(case.1)
        self.op(RETURN)
        self.op(END)
      }
      self.local_get(self.scratch)
      self.call("cpu_unresolved_jump")
      self.op(UNREACHABLE)
    }
    _ => raise WasmError("Not an exit")
  }
}

priv struct Shape {
  rpo : Array[Int] // order number per block, -1 when unreachable
  children : Array[Array[Int]] // dominator tree, in reverse postorder
  merge : Array[Bool]
  header : Array[Bool]
}

fn shape(f : @ir.Function) -> Shape? {
  let n = f.blocks.length()
  let post = []
  let seen = Array::make(n, false)
  // iterative DFS for postorder
  let stack : Array[(Int, Int)] = [(f.entry, 0)]
  seen[f.entry] = true
  while stack.length() > 0 {
    let (b, i) = stack[stack.length() - 1]
    let succ = f.blocks[b].term.successors()
    if i < succ.length() {
      stack[stack.length() - 1] = (b, i + 1)
      let s = succ[i]
      if !seen[s] {
        seen[s] = true
        stack.push((s, 0))
      }
    } else {
      post.push(b)
      stack.pop() |> ignore
    }
  }
  let order = post.copy()
  order.rev_in_place()
  let rpo = Array::make(n, -1)
  for i, b in order { rpo[b] = i }
  let preds : Array[Array[Int]] = Array::makei(n, fn(_) { [] })
  for b in order {
    for s in f.blocks[b].term.successors() {
      if !preds[s].contains(b) { preds[s].push(b) }
    }
  }
  // Cooper, Harvey and Kennedy's iterative dominators
  let idom = Array::make(n, -1)
  idom[f.entry] = f.entry
  let mut changed = true
  while changed {
    changed = false
    for b in order {
      if b == f.entry { continue }
      let mut new = -1
      for p in preds[b] {
        if idom[p] < 0 { continue }
        if new < 0 {
          new = p
        } else {
          let mut x = p
          let mut y = new
          while x != y {
            while rpo[x] > rpo[y] { x = idom[x] }
            while rpo[y] > rpo[x] { y = idom[y] }
          }
          new = x
        }
      }
      if new != idom[b] {
        idom[b] = new
        changed = true
      }
    }
  }
  fn dominates(d : Int, b : Int) -> Bool {
    let mut x = b
    while true {
      if x == d { return true }
      if x == idom[x] { return false }
      x = idom[x]
    }
    false
  }
  let merge = Array::make(n, false)
  let header = Array::make(n, false)
  for b in order {
    let mut forward = 0
    for p in preds[b] {
      if rpo[p] >= rpo[b] {
        // a retreating edge: reducible only if it is a back edge
        if !dominates(b, p) { return None }
        header[b] = true
      } else {
        forward += 1
      }
    }
    merge[b] = forward >= 2
  }
  let children : Array[Array[Int]] = Array::makei(n, fn(_) { [] })
  for b in order {
    if b != f.entry { children[idom[b]].push(b) }
  }
  Some({ rpo, children, merge, header })
}

fn depth(ctx : @list.List[Ctx], wanted : (Ctx) -> Bool) -> Int raise WasmError {
  let mut i = 0
  for c in ctx {
    if wanted(c) { return i }
    i += 1
  }
  raise WasmError("Branch target not in scope")
}

fn Gen::br(self : Gen, d : Int) -> Unit {
  self.op(BR)
  self.code.u32(d)
}

fn Gen::do_tree(self : Gen, sh : Shape, x : Int, ctx : @list.List[Ctx]) -> Unit raise WasmError {
  let merges = sh.children[x].filter(fn(c) { sh.merge[c] })
  // highest reverse-postorder number first: the outermost block
  merges.sort_by(fn(p, q) { sh.rpo[q] - sh.rpo[p] })
  if sh.header[x] {
    self.op(LOOP)
    self.code.byte(VOID)
    self.node_within(sh, x, merges, 0, ctx.add(LoopHeadedBy(x)))
    self.op(END)
  } else {
    self.node_within(sh, x, merges, 0, ctx)
  }
}

fn Gen::node_within(self : Gen, sh : Shape, x : Int, merges : Array[Int], i : Int, ctx : @list.List[Ctx]) -> Unit raise WasmError {
  if i < merges.length() {
    let y = merges[i]
    self.op(BLOCK)
    self.code.byte(VOID)
    self.node_within(sh, x, merges, i + 1, ctx.add(BlockFollowedBy(y)))
    self.op(END)
    self.do_tree(sh, y, ctx)
    return
  }
  let block = self.f.blocks[x]
  for s in block.stmts { self.stmt(s) }
  match block.term {
    Goto(t) => self.do_branch(sh, x, t, ctx)
    Branch(_, t, e) if t == e => self.do_branch(sh, x, t, ctx)
    Branch(c, t, e) => {
      self.expr(c)
      self.op(IF)
      self.code.byte(VOID)
      self.do_branch(sh, x, t, ctx.add(IfThenElse))
      self.op(ELSE)
      self.do_branch(sh, x, e, ctx.add(IfThenElse))
      self.op(END)
    }
    term => self.exit(term)
  }
}

fn Gen::do_branch(self : Gen, sh : Shape, source : Int, target : Int, ctx : @list.List[Ctx]) -> Unit raise WasmError {
  if sh.rpo[target] <= sh.rpo[source] {
    self.br(depth(ctx, fn(c) { c is LoopHeadedBy(h) && h == target }))
  } else if sh.merge[target] {
    self.br(depth(ctx, fn(c) { c is BlockFollowedBy(m) && m == target }))
  } else {
    self.do_tree(sh, target, ctx)
  }
}

// Irreducible control flow: a loop around a br_table on the block number.
fn Gen::dispatch_loop(self : Gen) -> Unit raise WasmError {
  let n = self.f.blocks.length()
  self.const_(self.f.entry)
  self.local_set(self.state)
  self.op(LOOP)
  self.code.byte(VOID)
  for _ in 0..<n {
    self.op(BLOCK)
    self.code.byte(VOID)
  }
  self.local_get(self.state)
  self.op(BR_TABLE)
  self.code.u32(n)
  for k in 0..<n { self.code.u32(k) }
  self.code.u32(n - 1)
  for b in 0..<n {
    self.op(END)
    // open: blocks b+1..n-1, then the loop
    let to_loop = n - 1 - b
    let block = self.f.blocks[b]
    for s in block.stmts { self.stmt(s) }
    let goto = fn(t : Int, extra : Int) {
      self.const_(t)
      self.local_set(self.state)
      self.br(to_loop + extra)
    }
    match block.term {
      Goto(t) => goto(t, 0)
      Branch(c, t, e) => {
        self.expr(c)
        self.op(IF)
        self.code.byte(VOID)
        goto(t, 1)
        self.op(ELSE)
        goto(e, 1)
        self.op(END)
      }
      term => self.exit(term)
    }
  }
  self.op(END)
}

pub(all) struct Output {
  bytes : Bytes
  irreducible : Array[String]
}

// Registers and flags live in locals: a function loads those live on entry,
// writes back the ones it changed where a callee, its caller or the idle
// loop reads them, and reloads after a call what the callee may change.
// `memory_vars` keeps them in memory instead, and `force_dispatch`
// structures every function with the dispatch loop (both to test those paths).
//
// `layout` gives the runtime's addresses of `ram`, `data` and the machine
// variables when they are known (the module is built after the runtime is
// instantiated); they become constants in memory offsets instead of
// imported globals.
pub fn emit(
  program : @ir.Program,
  layout~ : Map[String, Int] = Map([]),
  force_dispatch~ : Bool = false,
  memory_vars~ : Bool = false,
) -> Output raise WasmError {
  let info = match program.info {
    Some(i) => i
    None => program.analyze()
  }
  let generated = program.functions.filter(fn(f) { !f.native })
  // Runtime functions and natives the code calls.
  let imports : Imports = {
    globals: [],
    global_index: Map([]),
    funcs: [],
    func_index: Map([]),
  }
  let objects = ["ram", "data"]
  for v in @ir.all_vars { objects.push(v.c_name()) }
  for name in objects {
    if !layout.contains(name) {
      imports.global_index[name] = imports.globals.length()
      imports.globals.push(name)
    }
  }
  let wanted : Array[String] = ["read_byte", "dynamic_ram_write", "cpu_yield", "cpu_unresolved_jump"]
  for f in generated {
    for block in f.blocks {
      for s in block.stmts {
        match s {
          Effect("write_joypads", _) => { wanted.push("write_joypad1"); wanted.push("write_joypad2") }
          Effect(name, _) => wanted.push(name)
          _ => ()
        }
      }
    }
  }
  // natives the generated code reaches, and the entry points
  let natives : Set[String] = Set([])
  for f in program.functions {
    if f.native { natives.add(f.name) }
  }
  let reached = [program.reset, program.nmi]
  for f in generated {
    for block in f.blocks {
      for s in block.stmts {
        if s is Call(g) { reached.push(g) }
      }
      match block.term {
        TailCall(g) => reached.push(g)
        Dispatch(_, _, cases) => for c in cases { reached.push(c.1) }
        _ => ()
      }
    }
  }
  for name in reached {
    if natives.contains(name) { wanted.push(name) }
  }
  for name in wanted {
    if !imports.func_index.contains(name) {
      imports.func_index[name] = imports.funcs.length()
      imports.funcs.push((name, runtime_type(name)))
    }
  }
  let defined : Map[String, Int] = Map([])
  for i, f in generated { defined[f.name] = imports.funcs.length() + i }

  let out = Buf::new()
  for b in [0x00, 0x61, 0x73, 0x6d, 0x01, 0x00, 0x00, 0x00] { out.byte(b) }
  // types
  let section = Buf::new()
  section.u32(types.length())
  for t in types {
    section.byte(0x60)
    section.u32(t.0.length())
    for p in t.0 { section.byte(p) }
    section.u32(t.1.length())
    for r in t.1 { section.byte(r) }
  }
  out.section(1, section)
  // imports
  let section = Buf::new()
  section.u32(1 + imports.globals.length() + imports.funcs.length())
  section.name("env")
  section.name("memory")
  section.byte(0x02)
  section.byte(0x00)
  section.u32(1)
  for g in imports.globals {
    section.name("env")
    section.name(g)
    section.byte(0x03)
    section.byte(I32)
    section.byte(0x00)
  }
  for func in imports.funcs {
    section.name("env")
    section.name(func.0)
    section.byte(0x00)
    section.u32(func.1)
  }
  out.section(2, section)
  // functions
  let section = Buf::new()
  section.u32(generated.length())
  for f in generated { section.u32(if f.helper { 3 } else { 0 }) }
  out.section(3, section)
  // exports
  let exports : Array[(String, Int)] = []
  for f in generated {
    if !f.helper { exports.push((f.name, defined[f.name])) }
  }
  fn index_of(name : String) -> Int {
    match defined.get(name) {
      Some(i) => i
      None => imports.func_index[name]
    }
  }
  exports.push(("cpu_reset_entry", index_of(program.reset)))
  exports.push(("cpu_nmi_entry", index_of(program.nmi)))
  let section = Buf::new()
  section.u32(exports.length())
  for e in exports {
    section.name(e.0)
    section.byte(0x00)
    section.u32(e.1)
  }
  out.section(7, section)
  // code
  let irreducible = []
  let section = Buf::new()
  section.u32(generated.length())
  for f in generated {
    let params = if f.helper { 1 } else { 0 }
    let locals = f.temps.length() + 2 + (if memory_vars { 0 } else { 10 })
    let mut assigned = 0
    let mut read = 0
    for block in f.blocks {
      for s in block.stmts {
        assigned = assigned | s.defs()
        read = read | s.uses()
      }
      for e in block.term.exprs() { read = read | e.vars() }
    }
    let gen : Gen = {
      imports,
      defined,
      f,
      code: Buf::new(),
      scratch: params + f.temps.length(),
      state: params + f.temps.length() + 1,
      vars: if memory_vars { -1 } else { params + f.temps.length() + 2 },
      layout,
      info,
      block_in: info.blocks_live(f),
      assigned,
      read,
    }
    // the variables live on entry start in their locals
    gen.reload(gen.block_in[f.entry])
    match (if force_dispatch { None } else { shape(f) }) {
      Some(sh) => gen.do_tree(sh, f.entry, @list.empty())
      None => {
        if !force_dispatch { irreducible.push(f.name) }
        gen.dispatch_loop()
      }
    }
    gen.op(UNREACHABLE)
    gen.op(END)
    let body = Buf::new()
    body.u32(1) // one run of i32 locals
    body.u32(locals)
    body.byte(I32)
    body.append(gen.code)
    section.u32(body.bytes.length())
    section.append(body)
  }
  out.section(10, section)
  // names, for stack traces
  let names = Buf::new()
  names.name("name")
  let sub = Buf::new()
  let total = imports.funcs.length() + generated.length()
  sub.u32(total)
  for i, func in imports.funcs {
    sub.u32(i)
    sub.name(func.0)
  }
  for f in generated {
    sub.u32(defined[f.name])
    sub.name(f.name)
  }
  names.byte(1)
  names.u32(sub.bytes.length())
  names.append(sub)
  out.section(0, names)
  { bytes: out.to_bytes(), irreducible }
}
