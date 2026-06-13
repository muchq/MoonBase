package com.muchq.pgn.lexer;

/**
 * A token produced by the lexer.
 *
 * @param type The type of token
 * @param value The string value of the token
 * @param line The line number (1-indexed)
 * @param column The column number (1-indexed)
 */
public record Token(TokenType type, String value, int line, int column) {

  @Override
  public String toString() {
    return String.format("%s('%s') at %d:%d", type, value, line, column);
  }
}
