
type! PipelineError {
  IOError(String)
  LexerError(@parse.LexerError)
  ParserError(@parse.ParserError)
  LoweringError(@lower.LoweringError)
} derive(Show)

fn read_rom() -> String!PipelineError {
  match @fs.read_file_to_string?(path="src/smb.asm") {
    Ok(asm) => asm
    Err(err) => raise IOError(Show::to_string(err))
  }
}

fn lex() -> Array[@parse.Token]!PipelineError {
  let asm = read_rom!()
  let lexer = @parse.Lexer::new(asm)

  match lexer.lex?() {
    Ok(tokens) => tokens
    Err(err) => raise LexerError(err)
  }
}

fn parse() -> Array[@parse.SourceItem]!PipelineError {
  let tokens = lex!()
  let parser = @parse.Parser::new(tokens)

  match parser.parse?() {
    Ok(instructions) => instructions
    Err(err) => raise ParserError(err)
  }
}

fn lower() -> @lower.AsmLowering!PipelineError {
  let instructions = parse!()
  let lowering = @lower.AsmLowering::new(instructions)
  match lowering.lower?() {
    Ok(_) => lowering
    Err(err) => raise LoweringError(err)
  }
}

fn transpile() -> @transpile.Transpiler!PipelineError {
  let low = lower!()
  match @transpile.Transpiler::new?(low) {
    Ok(tr) => tr
    Err(err) => raise IOError(Show::to_string(err))
  }
}

let lib_output_dir: String = "out/lib"
let output_dir: String = "out"

fn main {
  if not(@fs.path_exists(path=output_dir)) {
    @fs.create_dir(path=output_dir)
  }

  if not(@fs.path_exists(path=lib_output_dir)) {
    @fs.create_dir(path=lib_output_dir)
  }

  match transpile?() {
      Ok(tr) => {
          for file in tr.files {
              file.write(dir=if file.is_lib { lib_output_dir } else { output_dir })
          }
      }
      Err(err) => {
          println(err)
      }
  }
}
