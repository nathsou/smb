// Inspects code discovery without generating code:
//
//   --input FILE.asm [--output prg.bin]   assemble, write the PRG image
//   --rom FILE.nes --map FILE.map         discover a ROM's code with a map
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
}

fn main {
  try {
    let rom = option("--rom", "")
    if rom != "" {
      let map = @image.parse_map(@fs.read_file_to_string(option("--map", "")))
      let dump = @image.load_dump(@fs.read_file_to_bytes(rom), map)
      let program = @image.discover_rom(@image.rom_image(dump.cartridge, map), map)
      if map.warning(dump) is Some(text) { println("Warning: \{text}") }
      report(program)
      return
    }
    let text = @fs.read_file_to_string(option("--input", ""))
    let source = @parse.Parser::new(@parse.Lexer::new(text).tokenize()).parse()
    let image = @image.assemble(source)
    println("PRG: origin=\{image.origin}, bytes=\{image.bytes.length()}")
    @fs.write_bytes_to_file(option("--output", "codegen/prg.bin"), Bytes::makei(image.bytes.length(), fn(i) { image.bytes[i].to_byte() }))
    report(@image.discover(image))
  } catch { error => { println(@debug.to_string(error)); abort("Inspection failed") } }
}
