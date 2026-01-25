package com.muchq.pgn.parser;

import com.muchq.pgn.lexer.Token;

/**
 * Exception thrown when the parser encounters invalid input.
 */
public class ParseException extends RuntimeException {
    private final Token token;

    public ParseException(String message, Token token) {
        super(String.format("%s at line %d, column %d (token: %s)",
            message, token.line(), token.column(), token.value()));
        this.token = token;
    }

    public ParseException(String message) {
        super(message);
        this.token = null;
    }

    public Token getToken() {
        return token;
    }
}
