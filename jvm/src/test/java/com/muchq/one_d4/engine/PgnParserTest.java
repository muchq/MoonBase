package com.muchq.one_d4.engine;

import static org.assertj.core.api.Assertions.assertThat;

import com.muchq.one_d4.engine.model.ParsedGame;
import org.junit.Test;

public class PgnParserTest {

  private final PgnParser parser = new PgnParser();

  @Test
  public void testParseHeaders() {
    String pgn =
        """
        [Event "Live Chess"]
        [Site "Chess.com"]
        [White "hikaru"]
        [Black "magnuscarlsen"]
        [Result "1-0"]
        [ECO "B90"]
        [WhiteElo "2850"]
        [BlackElo "2830"]

        1. e4 c5 2. Nf3 d6 3. d4 cxd4 4. Nxd4 Nf6 5. Nc3 a6 1-0
        """;

    ParsedGame game = parser.parse(pgn);
    assertThat(game.headers()).containsEntry("Event", "Live Chess");
    assertThat(game.headers()).containsEntry("White", "hikaru");
    assertThat(game.headers()).containsEntry("Black", "magnuscarlsen");
    assertThat(game.headers()).containsEntry("ECO", "B90");
    assertThat(game.headers()).containsEntry("WhiteElo", "2850");
  }

  @Test
  public void testParseMoveText() {
    String pgn =
        """
        [Event "Test"]

        1. e4 e5 2. Nf3 Nc6 3. Bb5 a6 1/2-1/2
        """;

    ParsedGame game = parser.parse(pgn);
    assertThat(game.moveText()).contains("1. e4 e5");
    assertThat(game.moveText()).contains("Bb5");
  }

  @Test
  public void testEmptyPgn() {
    ParsedGame game = parser.parse("");
    assertThat(game.headers()).isEmpty();
    assertThat(game.moveText()).isEmpty();
  }

  @Test
  public void testHeadersOnly() {
    String pgn =
        """
        [Event "Test"]
        [White "player1"]
        """;

    ParsedGame game = parser.parse(pgn);
    assertThat(game.headers()).hasSize(2);
    assertThat(game.moveText()).isEmpty();
  }
}
