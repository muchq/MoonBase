package com.muchq.games.chessql.parser;

import com.muchq.games.chessql.ast.AndExpr;
import com.muchq.games.chessql.ast.ComparisonExpr;
import com.muchq.games.chessql.ast.Expr;
import com.muchq.games.chessql.ast.InExpr;
import com.muchq.games.chessql.ast.MotifExpr;
import com.muchq.games.chessql.ast.NotExpr;
import com.muchq.games.chessql.ast.OrExpr;
import com.muchq.games.chessql.ast.OrderByClause;
import com.muchq.games.chessql.ast.SequenceExpr;
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

  public static ParsedQuery parse(String input) {
    List<Token> tokens = new Lexer(input).tokenize();
    Parser parser = new Parser(tokens);
    Expr expr = parser.parseExpr();
    OrderByClause orderBy = null;
    if (parser.check(TokenType.ORDER)) {
      parser.advance();
      parser.expect(TokenType.BY);
      orderBy = parser.parseOrderByClause();
    }
    if (!parser.check(TokenType.EOF)) {
      // Trailing input. When it could start another condition the mistake is almost always a
      // missing connector, so say so; a stray ')' or ',' gets no hint rather than a wrong one.
      Token t = parser.current();
      String hint = startsACondition(t.type()) ? " — combine conditions with AND or OR" : "";
      throw new ParseException("Expected end of query, got " + describe(t) + hint, t.position());
    }
    return new ParsedQuery(expr, orderBy);
  }

  private static boolean startsACondition(TokenType type) {
    return type == TokenType.IDENTIFIER
        || type == TokenType.MOTIF
        || type == TokenType.SEQUENCE
        || type == TokenType.NOT
        || type == TokenType.LPAREN;
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

    if (check(TokenType.SEQUENCE)) {
      return parseSequence();
    }

    if (check(TokenType.IDENTIFIER)) {
      return parseFieldExpr();
    }

    throw new ParseException(
        "Expected a condition — a field comparison like white.elo >= 2500, motif(...),"
            + " sequence(...), or a parenthesized expression — got "
            + describe(current()),
        current().position());
  }

  private Expr parseMotif() {
    advance(); // consume 'motif'
    expect(TokenType.LPAREN);
    Token name = expect(TokenType.IDENTIFIER);
    expect(TokenType.RPAREN);
    return new MotifExpr(name.value());
  }

  private Expr parseSequence() {
    advance(); // consume 'sequence'
    expect(TokenType.LPAREN);
    List<String> names = new ArrayList<>();
    names.add(expect(TokenType.IDENTIFIER).value());
    while (check(TokenType.THEN)) {
      advance();
      names.add(expect(TokenType.IDENTIFIER).value());
    }
    expect(TokenType.RPAREN);
    return new SequenceExpr(names);
  }

  private OrderByClause parseOrderByClause() {
    expect(TokenType.MOTIF_COUNT);
    expect(TokenType.LPAREN);
    Token name = expect(TokenType.IDENTIFIER);
    expect(TokenType.RPAREN);
    boolean ascending = true;
    if (check(TokenType.DESC)) {
      advance();
      ascending = false;
    } else if (check(TokenType.ASC)) {
      advance();
    }
    return new OrderByClause(name.value(), ascending);
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
      default ->
          throw new ParseException(
              "Expected a comparison operator (=, !=, <, <=, >, >=) or IN, got " + describe(t),
              t.position());
    };
  }

  // Values are where SQL habits land, so the two commonest mistakes get their own message: NULL
  // (which the language deliberately lacks) and an unquoted string. These strings are user-facing
  // — the web UI and MCP clients render them verbatim — so they speak ChessQL, not parser
  // internals.
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
    if (t.type() == TokenType.IDENTIFIER && t.value().equalsIgnoreCase("null")) {
      throw new ParseException(
          "ChessQL has no NULL literal — a game whose field is unset never matches any comparison,"
              + " so there is nothing to compare NULL against. Expected a number or a double-quoted"
              + " string",
          t.position());
    }
    if (t.type() == TokenType.IDENTIFIER) {
      throw new ParseException(
          "Expected a number or a double-quoted string, got "
              + describe(t)
              + " — strings must be double-quoted: \""
              + t.value()
              + "\"",
          t.position());
    }
    throw new ParseException(
        "Expected a number or a double-quoted string, got " + describe(t), t.position());
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
      throw new ParseException("Expected " + describe(type) + ", got " + describe(t), t.position());
    }
    return advance();
  }

  private static String describe(Token t) {
    return t.type() == TokenType.EOF ? "end of query" : "'" + t.value() + "'";
  }

  private static String describe(TokenType type) {
    return switch (type) {
      case NUMBER -> "a number";
      case STRING -> "a double-quoted string";
      case IDENTIFIER -> "a name";
      case EQ -> "'='";
      case NEQ -> "'!='";
      case LT -> "'<'";
      case LTE -> "'<='";
      case GT -> "'>'";
      case GTE -> "'>='";
      case AND -> "AND";
      case OR -> "OR";
      case NOT -> "NOT";
      case IN -> "IN";
      case MOTIF -> "motif";
      case ORDER -> "ORDER";
      case BY -> "BY";
      case ASC -> "ASC";
      case DESC -> "DESC";
      case MOTIF_COUNT -> "motif_count";
      case SEQUENCE -> "sequence";
      case THEN -> "THEN";
      case LPAREN -> "'('";
      case RPAREN -> "')'";
      case LBRACKET -> "'['";
      case RBRACKET -> "']'";
      case COMMA -> "','";
      case DOT -> "'.'";
      case EOF -> "end of query";
    };
  }
}
