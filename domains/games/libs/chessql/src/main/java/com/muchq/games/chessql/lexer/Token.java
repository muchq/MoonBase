package com.muchq.games.chessql.lexer;

public record Token(TokenType type, String value, int position) {
  @Override
  public String toString() {
    return "Token(" + type + ", " + value + ", pos=" + position + ")";
  }
}
