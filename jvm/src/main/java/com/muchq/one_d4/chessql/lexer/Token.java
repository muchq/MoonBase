package com.muchq.one_d4.chessql.lexer;

public record Token(TokenType type, String value, int position) {
    @Override
    public String toString() {
        return "Token(" + type + ", " + value + ", pos=" + position + ")";
    }
}
