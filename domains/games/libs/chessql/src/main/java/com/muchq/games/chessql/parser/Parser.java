package com.muchq.games.chessql.parser;

import com.muchq.games.chessql.ast.AndExpr;
import com.muchq.games.chessql.ast.ComparisonExpr;
import com.muchq.games.chessql.ast.Expr;
import com.muchq.games.chessql.ast.InExpr;
import com.muchq.games.chessql.ast.MotifExpr;
import com.muchq.games.chessql.ast.NotExpr;
import com.muchq.games.chessql.ast.OrExpr;
import com.muchq.games.chessql.lexer.Lexer;
import com.muchq.games.chessql.lexer.Token;
import com.muchq.games.chessql.lexer.TokenType;
import java.util.ArrayList;
import java.util.List;

public class Parser {
  private final List<Token> tokens;
  private int pos;

  public Parser(List<Token> tokens) {
    this.tokens = tokens;
    this.pos = 0;
  }

  public static Expr parse(String input) {
    List<Token> tokens = new Lexer(input).tokenize();
    Parser parser = new Parser(tokens);
    Expr expr = parser.parseExpr();
    parser.expect(TokenType.EOF);
    return expr;
  }

  public Expr parseExpr() {
    return parseOr();
  }

  private Expr parseOr() {
    Expr left = parseAnd();
    List<Expr> operands = new ArrayList<>();
    operands.add(left);

    while (check(TokenType.OR)) {
      advance();
      operands.add(parseAnd());
    }

    return operands.size() == 1 ? operands.get(0) : new OrExpr(operands);
  }

  private Expr parseAnd() {
    Expr left = parseNot();
    List<Expr> operands = new ArrayList<>();
    operands.add(left);

    while (check(TokenType.AND)) {
      advance();
      operands.add(parseNot());
    }

    return operands.size() == 1 ? operands.get(0) : new AndExpr(operands);
  }

  private Expr parseNot() {
    if (check(TokenType.NOT)) {
      advance();
      return new NotExpr(parseNot());
    }
    return parsePrimary();
  }

  private Expr parsePrimary() {
    if (check(TokenType.LPAREN)) {
      advance();
      Expr expr = parseExpr();
      expect(TokenType.RPAREN);
      return expr;
    }

    if (check(TokenType.MOTIF)) {
      return parseMotif();
    }

    if (check(TokenType.IDENTIFIER)) {
      return parseFieldExpr();
    }

    throw new ParseException("Unexpected token: " + current(), current().position());
  }

  private Expr parseMotif() {
    advance(); // consume 'motif'
    expect(TokenType.LPAREN);
    Token name = expect(TokenType.IDENTIFIER);
    expect(TokenType.RPAREN);
    return new MotifExpr(name.value());
  }

  private Expr parseFieldExpr() {
    String field = parseFieldName();

    if (check(TokenType.IN)) {
      advance();
      return parseInValues(field);
    }

    String op = parseCompOp();
    Object value = parseValue();
    return new ComparisonExpr(field, op, value);
  }

  private String parseFieldName() {
    Token first = expect(TokenType.IDENTIFIER);
    StringBuilder sb = new StringBuilder(first.value());

    while (check(TokenType.DOT)) {
      advance();
      Token next = expect(TokenType.IDENTIFIER);
      sb.append('.').append(next.value());
    }

    return sb.toString();
  }

  private String parseCompOp() {
    Token t = current();
    return switch (t.type()) {
      case EQ -> {
        advance();
        yield "=";
      }
      case NEQ -> {
        advance();
        yield "!=";
      }
      case LT -> {
        advance();
        yield "<";
      }
      case LTE -> {
        advance();
        yield "<=";
      }
      case GT -> {
        advance();
        yield ">";
      }
      case GTE -> {
        advance();
        yield ">=";
      }
      default -> throw new ParseException("Expected comparison operator, got: " + t, t.position());
    };
  }

  private Object parseValue() {
    Token t = current();
    if (t.type() == TokenType.NUMBER) {
      advance();
      return Integer.parseInt(t.value());
    }
    if (t.type() == TokenType.STRING) {
      advance();
      return t.value();
    }
    throw new ParseException("Expected value, got: " + t, t.position());
  }

  private InExpr parseInValues(String field) {
    expect(TokenType.LBRACKET);
    List<Object> values = new ArrayList<>();
    values.add(parseValue());
    while (check(TokenType.COMMA)) {
      advance();
      values.add(parseValue());
    }
    expect(TokenType.RBRACKET);
    return new InExpr(field, values);
  }

  private Token current() {
    return tokens.get(pos);
  }

  private boolean check(TokenType type) {
    return current().type() == type;
  }

  private Token advance() {
    Token t = tokens.get(pos);
    pos++;
    return t;
  }

  private Token expect(TokenType type) {
    Token t = current();
    if (t.type() != type) {
      throw new ParseException("Expected " + type + ", got " + t.type(), t.position());
    }
    return advance();
  }
}
