// Command line:
//
//   --rom FILE.nes --map FILE.map   recompile a ROM image named by a map
//   --input FILE.asm                or assemble a source (the test fixtures)
//   --output DIR                    where the C goes (default codegen/game)
//   --map FILE --header FILE        only write the map's names as a C header
//   --native FILE                   routines written by hand (see routines.txt)
//   --reference                     the unoptimised IR (differential test)
//   --wasm FILE                     also emit the WebAssembly game module
//     --wasm-layout FILE            "name address" lines: the runtime's addresses
//     --wasm-dispatch               every function as a dispatch loop
//     --wasm-memory-vars            registers and flags in memory, not locals
//     --wasm-no-inline              no inlining of small routines

fn option(name : String, fallback : String) -> String {
  let args = @env.args()
  for i in 0..<(args.length() - 1) {
    if args[i] == name { return args[i + 1] }
  }
  fallback
}

fn flag(name : String) -> Bool {
  @env.args().contains(name)
}

fn run() -> Unit raise Error {
  let header_path = option("--header", "")
  if header_path != "" {
    let map_path = option("--map", "")
    @fs.write_string_to_file(header_path, @pipeline.map_header(@fs.read_file_to_string(map_path), source=map_path))
    return
  }
  let rom_path = option("--rom", "")
  let program = if rom_path != "" {
    let map_path = option("--map", "")
    if map_path == "" { fail("--rom needs --map") }
    let (program, warning) = @pipeline.from_rom(@fs.read_file_to_bytes(rom_path), @fs.read_file_to_string(map_path))
    if warning is Some(text) { println("Warning: \{text}") }
    program
  } else {
    let input = option("--input", "")
    if input == "" { fail("Give --rom and --map, or --input") }
    @pipeline.from_assembly(@fs.read_file_to_string(input))
  }
  let native_path = option("--native", "")
  let native = if native_path == "" { [] } else { @pipeline.parse_names(@fs.read_file_to_string(native_path)) }
  let layout : Map[String, Int] = Map([])
  let layout_path = option("--wasm-layout", "")
  if layout_path != "" {
    for line in @fs.read_file_to_string(layout_path).split("\n") {
      let fields = line.split(" ").filter(fn(f) { !f.is_empty() }).map(fn(f) { f.to_owned() }).to_array()
      if fields.length() == 2 { layout[fields[0]] = @string.parse_int(fields[1]) }
    }
  }
  let wasm_path = option("--wasm", "")
  let output = @pipeline.recompile(program, {
    native,
    reference: flag("--reference"),
    wasm: wasm_path != "",
    layout,
    force_dispatch: flag("--wasm-dispatch"),
    memory_vars: flag("--wasm-memory-vars"),
    inline: !flag("--wasm-no-inline"),
  })
  let output_dir = option("--output", "codegen/game")
  if !@fs.path_exists(output_dir) { @fs.create_dir(output_dir) }
  for file in output.files { file.write(dir=output_dir) }
  if output.wasm is Some(bytes) {
    if !output.irreducible.is_empty() {
      println("Irreducible control flow (dispatch loop): \{output.irreducible.join(", ")}")
    }
    @fs.write_bytes_to_file(wasm_path, bytes)
  }
}

fn main {
  try run() catch {
    err => {
      println(@debug.to_string(err))
      abort("Code generation failed")
    }
  }
}
