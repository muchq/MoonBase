package com.muchq.pgn;

import com.muchq.pgn.lexer.PgnLexer;
import com.muchq.pgn.model.PgnGame;
import com.muchq.pgn.parser.PgnParser;

import java.util.List;

/**
 * High-level API for parsing PGN strings.
 *
 * Usage:
 *   PgnGame game = PgnReader.parseGame(pgnString);
 *   List<PgnGame> games = PgnReader.parseAll(pgnString);
 */
public final class PgnReader {

    private PgnReader() {
        // Utility class
    }

    /**
     * Parse a single game from PGN text.
     *
     * @param pgn The PGN string
     * @return The parsed game
     */
    public static PgnGame parseGame(String pgn) {
        var lexer = new PgnLexer(pgn);
        var tokens = lexer.tokenize();
        var parser = new PgnParser(tokens);
        return parser.parseGame();
    }

    /**
     * Parse all games from PGN text.
     *
     * @param pgn The PGN string (may contain multiple games)
     * @return List of parsed games
     */
    public static List<PgnGame> parseAll(String pgn) {
        var lexer = new PgnLexer(pgn);
        var tokens = lexer.tokenize();
        var parser = new PgnParser(tokens);
        return parser.parseAll();
    }
}
