package com.muchq.pgn.lexer;

public enum TokenType {
    // Delimiters
    LEFT_BRACKET,      // [
    RIGHT_BRACKET,     // ]
    LEFT_PAREN,        // (
    RIGHT_PAREN,       // )

    // Literals
    STRING,            // "quoted string"
    INTEGER,           // 1, 2, 15, etc.
    SYMBOL,            // Tag names, moves (e4, Nf3, O-O, O-O-O)

    // Move notation
    PERIOD,            // .
    ELLIPSIS,          // ...

    // Annotations
    NAG,               // $1, $2, etc.
    COMMENT,           // {comment text}

    // Game results
    RESULT,            // 1-0, 0-1, 1/2-1/2, *

    // End of file
    EOF
}
