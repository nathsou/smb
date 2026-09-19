fn main {
  try {
    let text = @fs.read_file_to_string("src/smb.asm")
    let source = @parse.Parser::new(@parse.Lexer::new(text).tokenize()).parse()
    let image = @image.assemble(source)
    println("PRG: origin=\{image.origin}, bytes=\{image.bytes.length()}")
    @fs.write_bytes_to_file("codegen/prg.bin", Bytes::makei(image.bytes.length(), fn(i) { image.bytes[i].to_byte() }))
    let program = @image.discover(image)
    program.validate_stacks()
    println("Decoded instructions: \{program.instructions.length()}, dispatch sites: \{program.dispatches.length()}, roots: \{@debug.to_string(program.roots)}")
  } catch { error => { println(@debug.to_string(error)); abort("Inspection failed") } }
}
