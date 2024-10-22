
type! PipelineError {
  IOError(String)
  LexerError(@parse.LexerError)
  ParserError(@parse.ParserError)
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

fn parse() -> Array[@parse.AsmInst]!PipelineError {
  let tokens = lex!()
  let parser = @parse.Parser::new(tokens)

  match parser.parse?() {
    Ok(instructions) => instructions
    Err(err) => raise ParserError(err)
  }
}

fn main {
    match parse?() {
        Ok(insts) => {
            for inst in insts {
                println(inst)
            }
        }
        Err(err) => {
            println(err)
        }
    }
}
