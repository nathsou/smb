
type! PipelineError {
  IOError(String)
  LexerError(String)
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
    Err(err) => raise LexerError(Show::to_string(err))
  }
}

fn main {
    match lex?() {
        Ok(tokens) => {
            for token in tokens {
                println(Show::to_string(token))
            }
        }
        Err(err) => {
            println(err)
        }
    }
}
