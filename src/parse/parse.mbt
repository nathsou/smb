pub(all) struct Parser {
  tokens: Array[Token]
  defines: Map[String, ConstantSize]
  mut index: Int
}

pub type! ParserError {
  UnexpectedToken(expected~: Token, received~: Token)
  ExpectedIdentifier(Token)
  ExpectedExpr(Token)
  Unreachable(String)
} derive(Show)

pub fn Parser::new(tokens: Array[Token]) -> Parser {
  { tokens, index: 0, defines: {} }
}

fn Parser::peek(self: Parser) -> Token {
  self.tokens[self.index]
}

fn Parser::lookahead(self: Parser, n: Int) -> Token {
  self.tokens[self.index + n]
}

fn Parser::advance(self: Parser) -> Unit {
  self.index += 1
}

fn expect(self: Parser, expected: Token) -> Unit!ParserError {
  let token = self.peek()
  if token == expected {
    self.advance()
  } else {
    raise UnexpectedToken(expected~, received=token)
  }
}

fn Parser::matches(self: Parser, token: Token) -> Bool {
  self.peek() == token
}

fn matches_predicate(self: Parser, predicate: (Token) -> Bool) -> Bool {
  predicate(self.peek())
}

fn Parser::consumes(self: Parser, token: Token) -> Bool {
  if self.matches(token) {
    self.advance()
    true
  } else {
    false
  }
}

fn Parser::consumeIfPresent(self: Parser, token: Token) -> Unit {
  let _ = self.consumes(token)

}

fn Parser::parse_identifier(self: Parser) -> String!ParserError {
  let token = self.peek()
  match token {
    Identifier(name) => {
      self.advance()
      name
    }
    _ => raise ExpectedIdentifier(token)
  }
}

fn Parser::parse_primary_expr(self: Parser) -> SourceExpr!ParserError {
  let token = self.peek()
  match token {
    Constant(base~, value~, size~) => {
      self.advance()
      Const(value~, base~, size~)
    }
    Identifier(name) => {
      self.advance()
      Ident(name)
    }
    LeftParen => {
      self.advance()
      let expr = self.parse_expr!()
      self.expect!(RightParen)
      expr
    }
    LeftChevron => {
      self.advance()
      let expr = self.parse_expr!()
      Unary(op=GetLowByte, expr~)
    }
    RightChevron => {
      self.advance()
      let expr = self.parse_expr!()
      Unary(op=GetHighByte, expr~)
    }
    _ => raise ExpectedExpr(token)
  }
}

fn Parser::parse_additive_expr(self: Parser) -> SourceExpr!ParserError {
  let mut lhs = self.parse_primary_expr!()
  while true {
    if self.consumes(Plus) {
      let rhs = self.parse_primary_expr!()
      lhs = Binary(op=Add, lhs~, rhs~)
    } else if self.consumes(Minus) {
      let rhs = self.parse_primary_expr!()
      lhs = Binary(op=Sub, lhs~, rhs~)
    } else {
      break
    }
  }
  lhs
}

fn Parser::parse_expr(self: Parser) -> SourceExpr!ParserError {
  self.parse_additive_expr!()
}

fn Parser::parse_directive_args(self: Parser) -> Array[SourceExpr]!ParserError {
  let args: Array[SourceExpr] = []
  while true {
    let expr = self.parse_expr!()
    args.push(expr)
    if not(self.consumes(Comma)) {
      break
    }
  }
  args
}

fn Parser::parse_addressing_mode(
  self: Parser,
  opcode~: OpCode
) -> SourceAddressingMode!ParserError {
  if opcode.is_implied() {
    Implied
  } else {
    let token = self.peek()
    match token {
      Hash => {
        self.advance()
        let expr = self.parse_expr!()
        Immediate(expr)
      }
      LeftParen => {
        self.advance()
        let addr = self.parse_expr!()

        if self.consumes(Comma) {
          if self.consumes(Identifier("x")) {
            return IndexedIndirect(addr~)
          } else {
            raise Unreachable("Expected 'x' after comma in indexed indirect addressing mode")
          }
        }

        self.expect!(RightParen)

        if self.consumes(Comma) {
          if self.consumes(Identifier("y")) {
            IndirectIndexed(addr~)
          } else {
            raise Unreachable("Expected 'y' after comma in indirect indexed addressing mode")
          }
        } else {
          Indirect(addr~)
        }
      }
      _ =>
        if opcode.is_relative() {
          let addr = self.parse_expr!()
          Relative(addr~)
        } else {
          match self.parse_expr?() {
            Ok(addr) =>
              if self.consumes(Comma) {
                if self.consumes(Identifier("x")) {
                  AbsoluteOrZeroPageX(addr~)
                } else if self.consumes(Identifier("y")) {
                  AbsoluteOrZeroPageY(addr~)
                } else {
                  raise Unreachable("Expected 'x' or 'y' after comma")
                }
              } else {
                AbsoluteOrZeroPage(addr~)
              }
            Err(err) =>
              if opcode.supports_accumulator_addr_mode() {
                Accumulator
              } else {
                raise err
              }
          }
        }
    }
  }
}

fn Parser::parse_inst(self: Parser) -> SourceItem!ParserError {
  let token = self.peek()

  match token {
    Identifier(name) => {
      self.advance()
      self.expect!(Equal)
      let rhs = self.parse_expr!()
      Define(name~, rhs~)
    }
    Label(name) => {
      self.advance()
      Label(name)
    }
    Directive(name) => {
      self.advance()
      let args = self.parse_directive_args!()
      Directive(name~, args~)
    }
    OpCode(op) => {
      self.advance()
      let mode = self.parse_addressing_mode!(opcode=op)
      Inst(op~, mode~)
    }
    Comment(comment~, is_inline~) => {
      self.advance()
      Comment(comment~, is_inline~)
    }
    _ => raise Unreachable("Expected instruction, got: \{token}")
  }
}

pub fn Parser::parse(self: Parser) -> Array[SourceItem]!ParserError {
  let insts: Array[SourceItem] = []

  while self.index < self.tokens.length() {
    let inst = self.parse_inst!()
    insts.push(inst)
  }

  insts
}
