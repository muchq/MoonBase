package com.muchq.pgn.parser;

import com.muchq.pgn.lexer.Token;
import com.muchq.pgn.model.PgnGame;
import java.util.List;

/**
 * Parses a list of tokens into PgnGame objects.
 *
 * <p>Usage: PgnParser parser = new PgnParser(tokens); PgnGame game = parser.parseGame();
 *
 * <p>Or for multiple games: List<PgnGame> games = parser.parseAll();
 */
public class PgnParser {
  private final List<Token> tokens;

  public PgnParser(List<Token> tokens) {
    this.tokens = tokens;
  }

  /**
   * Parse a single game from the token stream.
   *
   * @return The parsed game
   * @throws ParseException if the input is malformed
   */
  public PgnGame parseGame() {
    throw new UnsupportedOperationException("TODO: implement");
  }

  /**
   * Parse all games from the token stream.
   *
   * @return List of parsed games
   * @throws ParseException if the input is malformed
   */
  public List<PgnGame> parseAll() {
    throw new UnsupportedOperationException("TODO: implement");
  }
}
