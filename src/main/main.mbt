// Optional paths make the compiler usable with independent NROM fixtures.
fn option(name: String, fallback: String) -> String {
  let args = @env.args()
  for i in 0..<(args.length() - 1) {
    if args[i] == name { return args[i + 1] }
  }
  fallback
}


suberror PipelineError {
  IOError(String)
  LexerError(@parse.LexerError)
  ParserError(@parse.ParserError)
  LoweringError(@lower.LoweringError)
  ImageError(@image.ImageError)
  TranspileError(@transpile.TranspileError)
  BuildError(@ir.BuildError)
  WasmError(@wasm.WasmError)
} derive(Debug)

fn read_rom() -> String raise PipelineError {
  try @fs.read_file_to_string(option("--input", "src/smb.asm")) catch {
    err => raise IOError(Show::to_string(err))
  } noraise {
    assembly => assembly
  }
}

fn tokenize() -> Array[@parse.Token] raise PipelineError {
  let assembly = read_rom()
  let lexer = @parse.Lexer::new(assembly)

  try lexer.tokenize() catch {
    err => raise LexerError(err)
  } noraise {
    tokens => tokens
  }
}

fn parse() -> Array[@parse.SourceItem] raise PipelineError {
  let tokens = tokenize()
  let parser = @parse.Parser::new(tokens)

  try parser.parse() catch {
    err => raise ParserError(err)
  } noraise {
    instructions => instructions
  }
}

// --rom FILE.nes --map FILE.map: recompile a ROM image named by a map
// instead of assembling --input.
fn rom_program(rom_path: String) -> @image.Program raise PipelineError {
  let map_path = option("--map", "")
  if map_path == "" { raise IOError("--rom needs --map") }
  let bytes = try @fs.read_file_to_bytes(rom_path) catch { err => raise IOError(Show::to_string(err)) }
  let map_text = try @fs.read_file_to_string(map_path) catch { err => raise IOError(Show::to_string(err)) }
  try {
    let map = @image.parse_map(map_text)
    @image.discover_rom(@image.rom_image(@image.load_ines(bytes), map), map)
  } catch {
    error => raise ImageError(error)
  }
}

fn lower() -> @lower.AsmLowering raise PipelineError {
  let rom_path = option("--rom", "")
  let program = if rom_path != "" {
    rom_program(rom_path)
  } else {
    let instructions = parse()
    try @image.discover(@image.assemble(instructions)) catch {
      error => raise ImageError(error)
    }
  }
  try program.validate_stacks() catch { error => raise ImageError(error) }
  let lowering = @lower.AsmLowering::new(program.source, program=Some(program))
  try lowering.lower() catch {
    err => raise LoweringError(err)
  } noraise {
    _ => lowering
  }
}

// --native FILE: routine names (one per line, # comments) implemented in
// hand-written C instead of being generated.
fn native_routines() -> Array[String] raise PipelineError {
  let path = option("--native", "")
  if path == "" { return [] }
  let text = try @fs.read_file_to_string(path) catch {
    err => raise IOError(Show::to_string(err))
  }
  let names = []
  for line in text.split("\n") {
    let line = match line.find("#") { Some(i) => line.view(end_offset=i); None => line }
    let name = line.trim().to_owned()
    if name != "" { names.push(name) }
  }
  names
}

fn transpile() -> @transpile.Transpiler raise PipelineError {
  let native = native_routines()
  let low = lower()
  let program = try @ir.build(low, native=Set::from_array(native)) catch { err => raise BuildError(err) }
  // --reference: every instruction with all of its effects, unoptimised
  // (the differential test's reference)
  if @env.args().contains("--reference") { program.analyze() |> ignore } else { program.optimize() }
  let c = try @transpile.Transpiler::transpile(low, program, native~) catch {
    err => raise TranspileError(err)
  }
  // --wasm FILE: also emit the WebAssembly module (--wasm-dispatch: with
  // every function structured as a dispatch loop; --wasm-memory-vars: with
  // registers and flags in memory rather than locals)
  let wasm_path = option("--wasm", "")
  if wasm_path != "" {
    let args = @env.args()
    // --wasm-layout FILE: "name address" lines, the runtime's addresses
    let layout : Map[String, Int] = Map([])
    let layout_path = option("--wasm-layout", "")
    if layout_path != "" {
      let text = try @fs.read_file_to_string(layout_path) catch { err => raise IOError(Show::to_string(err)) }
      for line in text.split("\n") {
        let fields = line.split(" ").filter(fn(f) { !f.is_empty() }).map(fn(f) { f.to_owned() }).to_array()
        if fields.length() == 2 {
          layout[fields[0]] = try @string.parse_int(fields[1]) catch { _ => raise IOError("Bad layout line: \{line}") }
        }
      }
    }
    // The WebAssembly build also inlines small leaf routines; the C keeps
    // one function per routine.
    if !args.contains("--reference") && !args.contains("--wasm-no-inline") {
      program.inline_leaves() |> ignore
      program.optimize()
    }
    let output = try @wasm.emit(program, layout~, force_dispatch=args.contains("--wasm-dispatch"), memory_vars=args.contains("--wasm-memory-vars")) catch {
      err => raise WasmError(err)
    }
    if !output.irreducible.is_empty() {
      println("Irreducible control flow (dispatch loop): \{output.irreducible.join(", ")}")
    }
    try @fs.write_bytes_to_file(wasm_path, output.bytes) catch { err => raise IOError(Show::to_string(err)) }
  }
  c
}

let output_dir: String = option("--output", "codegen")
let lib_output_dir: String = "\{output_dir}/lib"

fn run() -> Unit raise Error {
  if !@fs.path_exists(output_dir) {
    @fs.create_dir(output_dir)
  }

  if !@fs.path_exists(lib_output_dir) {
    @fs.create_dir(lib_output_dir)
  }

  try transpile() catch {
      err => {
          println(@debug.to_string(err))
          abort("Code generation failed")
      }
  } noraise {
      tr => {
          for file in tr.files {
              file.write(dir=if file.is_lib { lib_output_dir } else { output_dir })
          }
      }
  }
}

fn main {
  try run() catch {
    err => {
      abort("Error: \{Show::to_string(err)}")
    }
  }
}
