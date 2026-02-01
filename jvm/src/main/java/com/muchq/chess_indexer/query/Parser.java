package com.muchq.chess_indexer.query;

import java.util.ArrayList;
import java.util.List;

public class Parser {

  private final List<Token> tokens;
  private int pos;

  public Parser(List<Token> tokens) {
    this.tokens = tokens;
  }

  public Expr parse() {
    Expr expr = parseOr();
    expect(TokenType.EOF);
    return expr;
  }

  private Expr parseOr() {
    Expr expr = parseAnd();
    while (match(TokenType.OR)) {
      Expr right = parseAnd();
      expr = new OrExpr(expr, right);
    }
    return expr;
  }

  private Expr parseAnd() {
    Expr expr = parseNot();
    while (match(TokenType.AND)) {
      Expr right = parseNot();
      expr = new AndExpr(expr, right);
    }
    return expr;
  }

  private Expr parseNot() {
    if (match(TokenType.NOT)) {
      return new NotExpr(parseNot());
    }
    return parsePrimary();
  }

  private Expr parsePrimary() {
    if (match(TokenType.LPAREN)) {
      Expr expr = parseOr();
      expect(TokenType.RPAREN);
      return expr;
    }

    if (peek(TokenType.IDENT)) {
      Token ident = advance();
      if (match(TokenType.LPAREN)) {
        List<Value> args = new ArrayList<>();
        if (!peek(TokenType.RPAREN)) {
          args.add(parseValue());
          while (match(TokenType.COMMA)) {
            args.add(parseValue());
          }
        }
        expect(TokenType.RPAREN);
        return new FuncCallExpr(ident.text(), args);
      }

      Field field = new Field(ident.text());
      if (match(TokenType.IN)) {
        expect(TokenType.LBRACKET);
        List<Value> values = new ArrayList<>();
        if (!peek(TokenType.RBRACKET)) {
          values.add(parseValue());
          while (match(TokenType.COMMA)) {
            values.add(parseValue());
          }
        }
        expect(TokenType.RBRACKET);
        return new InExpr(field, values);
      }

      if (peek(TokenType.OP)) {
        CompareOp op = parseOp(advance().text());
        Value value = parseValue();
        return new CompareExpr(field, op, value);
      }

      throw new IllegalArgumentException("Expected operator or function call after field: " + ident.text());
    }

    throw new IllegalArgumentException("Unexpected token: " + peek());
  }

  private CompareOp parseOp(String op) {
    return switch (op) {
      case "=" -> CompareOp.EQ;
      case "!=" -> CompareOp.NE;
      case "<" -> CompareOp.LT;
      case "<=" -> CompareOp.LTE;
      case ">" -> CompareOp.GT;
      case ">=" -> CompareOp.GTE;
      default -> throw new IllegalArgumentException("Unsupported operator: " + op);
    };
  }

  private Value parseValue() {
    Token token = advance();
    return switch (token.type()) {
      case STRING -> new StringValue(token.text());
      case NUMBER -> new NumberValue(Long.parseLong(token.text()));
      case BOOLEAN -> new BooleanValue(Boolean.parseBoolean(token.text()));
      case IDENT -> new IdentValue(token.text());
      default -> throw new IllegalArgumentException("Unexpected value token: " + token.type());
    };
  }

  private boolean match(TokenType type) {
    if (peek(type)) {
      pos++;
      return true;
    }
    return false;
  }

  private boolean peek(TokenType type) {
    return tokens.get(pos).type() == type;
  }

  private Token peek() {
    return tokens.get(pos);
  }

  private Token advance() {
    return tokens.get(pos++);
  }

  private void expect(TokenType type) {
    if (!match(type)) {
      throw new IllegalArgumentException("Expected token " + type + " but found " + peek().type());
    }
  }
}
