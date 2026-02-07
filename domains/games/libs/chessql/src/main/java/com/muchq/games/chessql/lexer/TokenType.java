package com.muchq.games.chessql.lexer;

public enum TokenType {
  // Literals
  NUMBER,
  STRING,
  IDENTIFIER,

  // Operators
  EQ, // =
  NEQ, // !=
  LT, // <
  LTE, // <=
  GT, // >
  GTE, // >=

  // Keywords
  AND,
  OR,
  NOT,
  IN,
  MOTIF,

  // Delimiters
  LPAREN,
  RPAREN,
  LBRACKET,
  RBRACKET,
  COMMA,
  DOT,

  // End
  EOF
}
