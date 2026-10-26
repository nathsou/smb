// The recompiler as a library, independent of the file system: the command
// line (src/main) and the page (src/web) both use it.

pub suberror PipelineError { PipelineError(String) } derive(Debug)

fn fail(stage : String, err : Error) -> PipelineError {
  PipelineError("\{stage}: \{@debug.to_string(err)}")
}

// `progress` is told each stage as it starts, by its key: "map", "rom" and
// "discover" here, then "stacks", "lower", "ir", "resumable", "optimize",
// "c", and for WebAssembly "inline" and "wasm" (the page shows them).
fn no_progress(_stage : String) -> Unit {
  ()
}

// A program discovered from a ROM (an iNES file or a raw dump) and a map,
// with a warning when the map was written for other dumps.
pub fn from_rom(
  rom : Bytes,
  map_text : String,
  progress~ : (String) -> Unit = no_progress,
) -> (@image.Program, String?) raise PipelineError {
  try {
    progress("map")
    let map = @image.parse_map(map_text)
    progress("rom")
    let dump = @image.load_dump(rom, map)
    let image = @image.rom_image(dump.cartridge, map)
    progress("discover")
    (@image.discover_rom(image, map), map.warning(dump))
  } catch {
    err => raise fail("ROM", err)
  }
}

// A program discovered from assembly source.
pub fn from_assembly(text : String) -> @image.Program raise PipelineError {
  let tokens = @parse.Lexer::new(text).tokenize() catch { err => raise fail("Lexer", err) }
  let items = @parse.Parser::new(tokens).parse() catch { err => raise fail("Parser", err) }
  @image.discover(@image.assemble(items)) catch { err => raise fail("Image", err) }
}

pub(all) struct Options {
  native : Array[String] // routines supplied by hand-written C
  reference : Bool // unoptimised IR (the differential test's reference)
  wasm : Bool
  layout : Map[String, Int] // runtime addresses for the WebAssembly module
  force_dispatch : Bool
  memory_vars : Bool
  inline : Bool
}

pub fn Options::default() -> Options {
  {
    native: [],
    reference: false,
    wasm: false,
    layout: Map([]),
    force_dispatch: false,
    memory_vars: false,
    inline: true,
  }
}

pub(all) struct Output {
  files : Array[@transpile.File] // the C program
  wasm : Bytes?
  irreducible : Array[String] // functions structured as dispatch loops
  // with preemption, where the foreground polls ([BANK:]ADDRESS, hex), for
  // the reference interpreter
  polls : Array[String]
}

pub fn recompile(
  program : @image.Program,
  options : Options,
  progress~ : (String) -> Unit = no_progress,
) -> Output raise PipelineError {
  progress("stacks")
  program.validate_stacks() catch { err => raise fail("Stack", err) }
  progress("lower")
  let low = @lower.AsmLowering::new(program.source, program=Some(program))
  low.lower() catch { err => raise fail("Lowering", err) }
  progress("ir")
  let ir = @ir.build(low, native=Set::from_array(options.native)) catch {
    err => raise fail("IR", err)
  }
  progress("resumable")
  let polls = ir.make_preemptible()
  ir.make_switchable() catch { err => raise fail("IR", err) }
  ir.make_restartable() catch { err => raise fail("IR", err) }
  ir.make_resumable() catch { err => raise fail("IR", err) }
  progress("optimize")
  if options.reference { ir.analyze() |> ignore } else { ir.optimize() }
  progress("c")
  let c = @transpile.Transpiler::transpile(low, ir, native=options.native) catch {
    err => raise fail("C", err)
  }
  let polls = polls.map(fn(location) {
    match @image.bank_of(location) {
      Some(bank) => "\{bank}:" + (location & 0xffff).to_string(radix=16)
      None => location.to_string(radix=16)
    }
  })
  if !options.wasm { return { files: c.files, wasm: None, irreducible: [], polls } }
  // The WebAssembly build also inlines small leaf routines; the C keeps one
  // function per routine.
  if !options.reference && options.inline {
    progress("inline")
    ir.inline_leaves() |> ignore
    ir.optimize()
  }
  progress("wasm")
  let output = @wasm.emit(
    ir,
    layout=options.layout,
    force_dispatch=options.force_dispatch,
    memory_vars=options.memory_vars,
  ) catch {
    err => raise fail("WebAssembly", err)
  }
  { files: c.files, wasm: Some(output.bytes), irreducible: output.irreducible, polls }
}

// Routine names, one per line, `#` comments.
pub fn parse_names(text : String) -> Array[String] {
  let names = []
  for line in text.split("\n") {
    let line = match line.find("#") {
      Some(i) => line.view(end_offset=i)
      None => line
    }
    let name = line.trim().to_owned()
    if name != "" { names.push(name) }
  }
  names
}

fn hex(v : Int) -> String {
  "0x" + v.to_string(radix=16)
}

// A C header with the map's names alone (no ROM needed): its types,
// addresses of variables, registers and tables, and prototypes of the
// routines, for the
// hand-written code that works alongside the generated program.
pub fn map_header(map_text : String, source~ : String) -> String raise PipelineError {
  let map = @image.parse_map(map_text) catch { err => raise fail("Map", err) }
  let out = StringBuilder()
  out.write_string("// The names in \{source}, for hand-written code: generated by\n")
  out.write_string("// `make header`, do not edit.\n\n#ifndef SMB_MAP_H\n#define SMB_MAP_H\n\n")
  for t in map.types { out.write_string(@transpile.type_definition(t) + "\n") }
  let seen : Set[String] = Set([])
  for entry in map.entries {
    if seen.contains(entry.name) { continue }
    seen.add(entry.name)
    let comment = if entry.description == "" { "" } else { " // " + entry.description }
    match entry.kind {
      Routine => out.write_string("void \{entry.name}(void);\{comment}\n")
      _ => out.write_string("#define \{entry.name} \{hex(entry.address)}\{comment}\n")
    }
  }
  out.write_string("\n#endif\n")
  out.to_string()
}
