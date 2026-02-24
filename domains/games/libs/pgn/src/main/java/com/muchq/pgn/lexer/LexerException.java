package com.muchq.pgn.lexer;

/** Exception thrown when the lexer encounters invalid input. */
public class LexerException extends RuntimeException {
  private final int line;
  private final int column;

  public LexerException(String message, int line, int column) {
    super(String.format("%s at line %d, column %d", message, line, column));
    this.line = line;
    this.column = column;
  }

  public int getLine() {
    return line;
  }

  public int getColumn() {
    return column;
  }
}
