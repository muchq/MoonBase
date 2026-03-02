package com.muchq.pgn.model;

public enum Piece {
  KING('K'),
  QUEEN('Q'),
  ROOK('R'),
  BISHOP('B'),
  KNIGHT('N'),
  PAWN('\0');

  private final char symbol;

  Piece(char symbol) {
    this.symbol = symbol;
  }

  public char symbol() {
    return symbol;
  }

  public static Piece fromSymbol(char c) {
    throw new UnsupportedOperationException("TODO: implement");
  }
}
