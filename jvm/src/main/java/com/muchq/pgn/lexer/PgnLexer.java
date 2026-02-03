package com.muchq.pgn.lexer;

import java.util.List;

/**
 * Tokenizes PGN input into a list of tokens.
 *
 * Usage:
 *   PgnLexer lexer = new PgnLexer(pgnString);
 *   List<Token> tokens = lexer.tokenize();
 */
public class PgnLexer {
    private final String input;

    public PgnLexer(String input) {
        this.input = input;
    }

    /**
     * Tokenize the input and return all tokens.
     * The last token will always be EOF.
     *
     * @return List of tokens
     * @throws LexerException if invalid input is encountered
     */
    public List<Token> tokenize() {
        throw new UnsupportedOperationException("TODO: implement");
    }
}
