
type! PipelineError {
  IOError(String)
  LexerError(@parse.LexerError)
  ParserError(@parse.ParserError)
  LoweringError(@lower.AsmLoweringError)
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

fn lower() -> Array[@lower.AsmItem]!PipelineError {
  let instructions = parse!()
  let lowering = @lower.AsmLowering::new(instructions)
  match lowering.lower?() {
    Ok(items) => items
    Err(err) => raise LoweringError(err)
  }
}

fn main {
    match lower?() {
        Ok(items) => {
          for item in items {
              println(item)
          }
        }
        Err(err) => {
            println(err)
        }
    }
}
