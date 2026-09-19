fn fixture(s: String) -> Image raise {
  assemble(@parse.Parser::new(@parse.Lexer::new(s).tokenize()).parse())
}

test "aliases, mixed data, forward expressions and origin gaps" {
  let image = fixture(".org $8000\nAlias:\nData:\n.db <Later, >Later\n.dw Later\n.org $8010\nLater:\n lda #$04\n rts\n")
  assert_eq(image.symbols["Alias"], image.symbols["Data"])
  assert_eq(image.word(0x8000), 0x8010)
  assert_eq(image.word(0x8002), 0x8010)
  assert_eq(image.read(0x800f), 0)
  assert_eq(image.read(0x8010), 0xa9)
}

test "an unavailable zero-page indexed form selects the absolute encoding" {
  let image = fixture(".org $8000\n cmp $10,y\nNext:\n rts\n")
  assert_eq(image.read(0x8000), 0xd9)
  assert_eq(image.symbols["Next"], 0x8003)
}

test "relative branch displacement uses the following instruction address" {
  let image = fixture(".org $8000\nLoop:\n dex\n bne Loop\n rts\n")
  assert_eq(image.read(0x8002), 0xfd)
}

test "all documented opcode encodings round trip" {
  let mut count = 0
  for i in 0..<256 {
    if decoding(i) is Some((op, mode)) {
      assert_eq(encoding(op, mode), Some(i))
      count += 1
    }
  }
  assert_eq(count, 151)
  assert_eq(decoding(0x02), None)
}
