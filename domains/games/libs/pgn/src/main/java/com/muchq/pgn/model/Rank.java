package com.muchq.pgn.model;

public enum Rank {
  R1(1),
  R2(2),
  R3(3),
  R4(4),
  R5(5),
  R6(6),
  R7(7),
  R8(8);

  private final int number;

  Rank(int number) {
    this.number = number;
  }

  public int number() {
    return number;
  }

  public static Rank fromNumber(int n) {
    throw new UnsupportedOperationException("TODO: implement");
  }

  public static Rank fromChar(char c) {
    throw new UnsupportedOperationException("TODO: implement");
  }

  public char toChar() {
    throw new UnsupportedOperationException("TODO: implement");
  }
}
