// Structure only single-entry regions. Checking all incoming edges is a
// sufficient dominance test for these contiguous regions; a crossing edge
// prevents folding instead of silently changing the meaning of a goto.
fn inverse(op: @parse.OpCode) -> @parse.OpCode {
  match op {
    BCC => BCS; BCS => BCC; BEQ => BNE; BNE => BEQ
    BMI => BPL; BPL => BMI; BVC => BVS; BVS => BVC
    _ => abort("Not a condition")
  }
}

fn AsmLowering::structure(self: AsmLowering, items: Array[AsmItem]) -> Array[AsmItem] {
  let labels: Map[String, Int] = Map([])
  for i, item in items { if item is Label(name) { labels[name] = i } }
  let edges: Array[(Int, Int)] = []
  for i, item in items {
    match item {
      Inst(op=JMP | BCC | BCS | BEQ | BNE | BMI | BPL | BVC | BVS,
        mode=Relative(Ident(name~, ..)) | Absolute(Ident(name~, ..)), ..) => {
        if labels.get(name) is Some(target) { edges.push((i, target)) }
      }
      _ => ()
    }
  }
  fn single_entry(start: Int, end: Int) -> Bool {
    !edges.iter().any(fn(edge) {
      let (from, to) = edge
      to > start && to < end && (from < start || from >= end)
    })
  }
  fn range(start: Int, end: Int) -> Array[AsmItem] {
    let result = []
    let mut i = start
    while i < end {
      let item = items[i]
      // A conditional backedge closes a natural single-entry loop. Keep the
      // original header label inside for any additional local backedges.
      if item is Label(name) {
        let mut loop_end: Int? = None
        for j in (i + 1)..<end {
          match items[j] {
            Inst(op~, mode=Relative(Ident(name=target, ..)), ..) if op.is_relative() && target == name && single_entry(i, j + 1) => loop_end = Some(j)
            _ => ()
          }
        }
        if loop_end is Some(j) {
          if items[j] is Inst(op~, comment~, ..) {
            let body = [item]
            for child in range(i + 1, j) { body.push(child) }
            result.push(DoWhile(cond=op, body~, comment~))
            i = j + 1
            continue
          }
        }
      }
      match item {
        Inst(op~, mode=Relative(Ident(name=target, ..)), comment~, ..) if op.is_relative() => {
          if target == "__ret__" {
            result.push(If(cond=op, then=[Inst(id=-1, op=RTS, mode=Implied, comment=None)], otherwise=None, comment~))
          } else if labels.get(target) is Some(j) && j > i && j < end && single_entry(i, j) {
            let mut tail = j - 1
            while tail > i && items[tail] is Comment(_) { tail -= 1 }
            if items[tail] is Inst(op=JMP, mode=Absolute(Ident(name=join, ..)), ..) {
              if labels.get(join) is Some(k) && k > j && k < end && single_entry(i, k) {
                result.push(If(cond=inverse(op), then=range(i + 1, tail),
                  otherwise=Some(range(j, k)), comment=comment.map(fn(text) { "Original branch: " + text })))
                i = k
                continue
              }
            }
            // The taken edge skips this body. All other entries have been
            // excluded, including edges originating in another branch arm.
            result.push(If(cond=inverse(op), then=range(i + 1, j), otherwise=None,
              comment=comment.map(fn(text) { "Original branch: " + text })))
            i = j
            continue
          } else if !labels.contains(target) && self.targets.subroutines.contains(target) {
            result.push(If(cond=op, then=[
              Inst(id=-1, op=JSR, mode=Absolute(Ident(name=target, ty=LabelTy)), comment=None),
              Inst(id=-1, op=RTS, mode=Implied, comment=None),
            ], otherwise=None, comment~))
          } else { result.push(item) }
        }
        Inst(op=JMP, mode=Absolute(Ident(name=target, ..)), comment~, ..) => {
          let mut next = i + 1
          while next < end && items[next] is Comment(_) { next += 1 }
          if labels.get(target) == Some(next) {
            if comment is Some(text) { result.push(Comment(text)) }
          } else { result.push(item) }
        }
        _ => result.push(item)
      }
      i += 1
    }
    result
  }
  range(0, items.length())
}
