package com.muchq.chess_indexer.query;

public enum CompareOp {
  EQ("="),
  NE("!=") ,
  LT("<"),
  LTE("<="),
  GT(">"),
  GTE(">=");

  private final String sql;

  CompareOp(String sql) {
    this.sql = sql;
  }

  public String sql() {
    return sql;
  }
}
