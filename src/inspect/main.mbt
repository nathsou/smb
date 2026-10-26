// Inspects code discovery without generating code:
//
//   --input FILE.asm [--output prg.bin]   assemble, write the PRG image
//     --bank                              only assemble (a bank of a banked
//                                         program: no vectors to start from)
//     --symbols                           print the labels' addresses
//   --rom FILE.nes --map FILE.map         discover a ROM's code with a map
//     --collect                           report every problem discovery can
//                                         step over, not only the first
//       --facts [BANK:]ADDR               and what discovery knows there
//       --calls ADDR                      and the calls to a routine
//       --switching [BANK:]ADDR           and why a routine switches banks
fn option(name: String, fallback: String) -> String {
  let args = @env.args()
  for i in 0..<(args.length() - 1) {
    if args[i] == name { return args[i + 1] }
  }
  fallback
}

fn hex(v: Int) -> String {
  "$" + (v & 0xffff).to_string(radix=16)
}

fn location(v: Int) -> String {
  match @image.bank_of(v) {
    Some(bank) => "\{bank}:\{hex(v)}"
    None => hex(v)
  }
}

fn report(program: @image.Program) -> Unit raise {
  program.validate_stacks()
  println("Decoded instructions: \{program.instructions.length()}, dispatch sites: \{program.dispatches.length()}, roots: \{@debug.to_string(program.roots)}")
  let windows = program.windows.keys().to_array()
  windows.sort()
  for w in windows {
    println("Window \{hex(w)}: \{program.windows[w].map(location).join(", ")}")
  }
  let jumps = program.jumps.keys().to_array()
  jumps.sort()
  for j in jumps {
    println("Computed jump at \{location(j)}: \{program.jumps[j].length()} targets")
  }
  let waits = program.waits.keys().to_array()
  waits.sort()
  for w in waits {
    println("Wait loop at \{location(program.waits[w])}, back from \{location(w)}")
  }
  // an idle loop (`jmp *`) waits too, back to itself
  let idle = program.idle.to_array()
  idle.sort()
  for pc in idle {
    println("Wait loop at \{location(pc)}, back from \{location(pc)}")
  }
}

fn main {
  try {
    let rom = option("--rom", "")
    if rom != "" {
      let map = @image.parse_map(@fs.read_file_to_string(option("--map", "")))
      let dump = @image.load_dump(@fs.read_file_to_bytes(rom), map)
      if @env.args().contains("--collect") { @image.collect_problems() }
      let program = @image.discover_rom(@image.rom_image(dump.cartridge, map), map)
      let problems = @image.problems()
      if !problems.is_empty() || @env.args().contains("--collect") {
        problems.sort()
        for p in problems { println(p) }
        println("\{problems.length()} problems")
        let waits = program.waits.keys().to_array()
        waits.sort()
        for w in waits { println("Wait loop at \{location(program.waits[w])}, back from \{location(w)}") }
        let at = option("--facts", "")
        if at != "" {
          let location = match at.find(":") {
            Some(i) => @image.bank_location(@string.parse_int(at.view(end_offset=i).to_owned()), @string.parse_int(at.view(start_offset=i + 1).to_owned(), base=16))
            None => @string.parse_int(at, base=16)
          }
          println(@debug.to_string(program.facts.get(location)))
        }
        let callee = option("--calls", "")
        if callee != "" {
          let target = @string.parse_int(callee, base=16)
          let sites = []
          for pc, inst in program.instructions {
            if inst.op == JSR && (@image.jump_target(inst) & 0xffff) == target { sites.push(pc) }
          }
          sites.sort()
          for site in sites { println("call \{location(site)}") }
        }
        let why = option("--switching", "")
        if why != "" {
          // BANK:ADDR or ADDR (hex)
          let location = match why.find(":") {
            Some(i) => @image.bank_location(@string.parse_int(why.view(end_offset=i).to_owned()), @string.parse_int(why.view(start_offset=i + 1).to_owned(), base=16))
            None => @string.parse_int(why, base=16)
          }
          for line in program.switching_chain(location) { println(line) }
        }
        return
      }
      if map.warning(dump) is Some(text) { println("Warning: \{text}") }
      report(program)
      return
    }
    let text = @fs.read_file_to_string(option("--input", ""))
    let source = @parse.Parser::new(@parse.Lexer::new(text).tokenize()).parse()
    let image = @image.assemble(source)
    println("PRG: origin=\{image.origin}, bytes=\{image.bytes.length()}")
    @fs.write_bytes_to_file(option("--output", "codegen/prg.bin"), Bytes::makei(image.bytes.length(), fn(i) { image.bytes[i].to_byte() }))
    if @env.args().contains("--symbols") {
      let names = image.symbols.keys().to_array()
      names.sort_by(fn(a, b) { image.symbols[a] - image.symbols[b] })
      for name in names { println("\{name} \{hex(image.symbols[name])}") }
    }
    if !@env.args().contains("--bank") { report(@image.discover(image)) }
  } catch { error => { println(@debug.to_string(error)); abort("Inspection failed") } }
}
