package com.muchq.chess_indexer.query;

import java.util.ArrayList;
import java.util.List;

public class Lexer {

  private final String input;
  private int pos;

  public Lexer(String input) {
    this.input = input;
  }

  public List<Token> tokenize() {
    List<Token> tokens = new ArrayList<>();
    while (pos < input.length()) {
      char c = input.charAt(pos);
      if (Character.isWhitespace(c)) {
        pos++;
        continue;
      }
      if (c == '(') {
        tokens.add(new Token(TokenType.LPAREN, "("));
        pos++;
        continue;
      }
      if (c == ')') {
        tokens.add(new Token(TokenType.RPAREN, ")"));
        pos++;
        continue;
      }
      if (c == '[') {
        tokens.add(new Token(TokenType.LBRACKET, "["));
        pos++;
        continue;
      }
      if (c == ']') {
        tokens.add(new Token(TokenType.RBRACKET, "]"));
        pos++;
        continue;
      }
      if (c == ',') {
        tokens.add(new Token(TokenType.COMMA, ","));
        pos++;
        continue;
      }
      if (c == '"') {
        tokens.add(new Token(TokenType.STRING, readString()));
        continue;
      }
      if (isOperatorStart(c)) {
        tokens.add(new Token(TokenType.OP, readOperator()));
        continue;
      }
      if (Character.isDigit(c)) {
        tokens.add(new Token(TokenType.NUMBER, readNumber()));
        continue;
      }
      if (isIdentStart(c)) {
        String ident = readIdent();
        tokens.add(keywordOrIdent(ident));
        continue;
      }
      throw new IllegalArgumentException("Unexpected character: " + c);
    }
    tokens.add(new Token(TokenType.EOF, ""));
    return tokens;
  }

  private boolean isIdentStart(char c) {
    return Character.isLetter(c) || c == '_' || c == '.';
  }

  private String readIdent() {
    int start = pos;
    while (pos < input.length()) {
      char c = input.charAt(pos);
      if (Character.isLetterOrDigit(c) || c == '_' || c == '.' || c == '-') {
        pos++;
      } else {
        break;
      }
    }
    return input.substring(start, pos);
  }

  private Token keywordOrIdent(String ident) {
    String upper = ident.toUpperCase();
    switch (upper) {
      case "AND":
        return new Token(TokenType.AND, ident);
      case "OR":
        return new Token(TokenType.OR, ident);
      case "NOT":
        return new Token(TokenType.NOT, ident);
      case "IN":
        return new Token(TokenType.IN, ident);
      case "TRUE":
      case "FALSE":
        return new Token(TokenType.BOOLEAN, ident.toLowerCase());
      default:
        return new Token(TokenType.IDENT, ident);
    }
  }

  private String readString() {
    pos++; // skip opening quote
    StringBuilder sb = new StringBuilder();
    while (pos < input.length()) {
      char c = input.charAt(pos++);
      if (c == '"') {
        break;
      }
      if (c == '\\' && pos < input.length()) {
        char next = input.charAt(pos++);
        sb.append(next);
      } else {
        sb.append(c);
      }
    }
    return sb.toString();
  }

  private String readNumber() {
    int start = pos;
    while (pos < input.length() && Character.isDigit(input.charAt(pos))) {
      pos++;
    }
    return input.substring(start, pos);
  }

  private boolean isOperatorStart(char c) {
    return c == '=' || c == '!' || c == '<' || c == '>';
  }

  private String readOperator() {
    int start = pos;
    pos++;
    if (pos < input.length()) {
      char next = input.charAt(pos);
      if ((input.charAt(start) == '!' || input.charAt(start) == '<' || input.charAt(start) == '>') && next == '=') {
        pos++;
      }
    }
    return input.substring(start, pos);
  }
}
