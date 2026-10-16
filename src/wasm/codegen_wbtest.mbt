test "LEB128 encodings" {
  let b = Buf::new()
  b.u32(624485)
  b.i32(-123456)
  b.i32(64)
  b.i32(-1)
  assert_eq(b.bytes, [0xe5, 0x8e, 0x26, 0xc0, 0xbb, 0x78, 0xc0, 0x00, 0x7f])
}

fn function_of(terms : Array[@ir.Term]) -> @ir.Function {
  {
    name: "f",
    blocks: terms.map(fn(t) { { label: "", stmts: [], term: t } }),
    entry: 0,
    temps: [],
    doc: [],
    native: false,
    helper: false,
  }
}

test "loops and merges are recognized; irreducible graphs are not structured" {
  // 0 -> 1 -> 2 -> 1 (loop), 2 -> 3; 0 -> 3 (merge)
  let reducible = function_of([
    Branch(Var(Z), 1, 3),
    Goto(2),
    Branch(Var(C), 1, 3),
    Return,
  ])
  match shape(reducible) {
    Some(sh) => {
      assert_eq(sh.header[1], true)
      assert_eq(sh.merge[3], true)
      assert_eq(sh.merge[1], false)
    }
    None => fail("expected a reducible graph")
  }
  // two entries into the cycle 1 <-> 2
  let irreducible = function_of([
    Branch(Var(Z), 1, 2),
    Branch(Var(C), 2, 3),
    Branch(Var(N), 1, 3),
    Return,
  ])
  assert_eq(shape(irreducible) is None, true)
}
