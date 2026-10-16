// The recompiler for the page (built with `moon build --target js`; see
// web/recompiler-worker.js). `recompile` returns an error message, or ""
// once the outputs are ready to read with the other functions.

let last : Ref[@pipeline.Output?] = Ref(None)
let last_warning : Ref[String] = Ref("")

// `layout`: "name address" lines, the runtime module's addresses.
pub fn recompile(rom : Bytes, map : String, native : String, layout : String) -> String {
  last.val = None
  last_warning.val = ""
  let addresses : Map[String, Int] = Map([])
  for line in layout.split("\n") {
    let fields = line.split(" ").filter(fn(f) { !f.is_empty() }).map(fn(f) { f.to_owned() }).to_array()
    if fields.length() == 2 {
      addresses[fields[0]] = @string.parse_int(fields[1]) catch { _ => return "Bad layout line: \{line}" }
    }
  }
  try {
    let (program, warning) = @pipeline.from_rom(rom, map)
    last_warning.val = warning.unwrap_or("")
    let options = { ..@pipeline.Options::default(), native: @pipeline.parse_names(native), wasm: true, layout: addresses }
    last.val = Some(@pipeline.recompile(program, options))
    ""
  } catch {
    @pipeline.PipelineError(message) => message
  }
}

// "" when the ROM is one the map was written for (or the map names none).
pub fn warning() -> String {
  last_warning.val
}

pub fn wasm() -> Bytes {
  match last.val {
    Some({ wasm: Some(bytes), .. }) => bytes
    _ => Bytes::new(0)
  }
}

pub fn file_count() -> Int {
  match last.val {
    Some(output) => output.files.length()
    None => 0
  }
}

pub fn file_name(i : Int) -> String {
  last.val.unwrap().files[i].name
}

pub fn file_contents(i : Int) -> String {
  last.val.unwrap().files[i].contents
}
