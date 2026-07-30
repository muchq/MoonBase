package com.muchq.games.one_d4.engine;

import static org.assertj.core.api.Assertions.assertThat;

import chariot.chess.Board;
import com.muchq.games.one_d4.engine.model.PositionContext;
import java.io.IOException;
import java.io.InputStream;
import java.nio.charset.StandardCharsets;
import java.util.List;
import java.util.regex.Matcher;
import java.util.regex.Pattern;
import org.junit.jupiter.api.Test;

/**
 * Replays a frozen bank of 500 real chess.com games (hikaru, June 2026, blitz + bullet — see
 * src/test/resources/hikaru_corpus.pgn) through the production pipeline and asserts FEN parity
 * against chariot at every ply, plus each game's final position against the PGN's own
 * [CurrentPosition] header. This is the wide-net complement to GameReplayerParityTest's hand-picked
 * edge cases: 42,706 plies of real play with clock comments, castling on both wings, promotions, en
 * passant, and rank/file disambiguation.
 *
 * <p>The chariot oracle replays the SANs recorded in {@link PositionContext}, i.e. exactly the
 * tokens the production replayer played, so that comparison isolates board mechanics from PGN
 * tokenizing. The tokenizing half is pinned separately: the exact total-ply count (the corpus is
 * frozen, so a tokenizer that drops or invents moves shifts the constant) and the [CurrentPosition]
 * finals, which come from chess.com's record rather than anything this codebase computed.
 */
public class HikaruCorpusParityTest {

  private static final int EXPECTED_TOTAL_PLIES = 42_706;

  private static final Pattern LINK = Pattern.compile("\\[Link \"([^\"]+)\"\\]");
  private static final Pattern CURRENT_POSITION =
      Pattern.compile("\\[CurrentPosition \"([^\"]+)\"\\]");

  @Test
  public void allCorpusGamesMatchChariotAtEveryPly() throws IOException {
    List<String> games = loadCorpus();
    assertThat(games).hasSize(500);

    PgnParser parser = new PgnParser();
    GameReplayer replayer = new GameReplayer();
    long totalPlies = 0;

    for (int i = 0; i < games.size(); i++) {
      String pgn = games.get(i);
      String label = gameLabel(pgn, i);

      List<PositionContext> positions;
      try {
        positions = replayer.replay(parser.parse(pgn).moveText());
      } catch (RuntimeException e) {
        throw new AssertionError("Replay failed for " + label + ": " + e.getMessage(), e);
      }
      totalPlies += positions.size() - 1;

      Board oracle = Board.ofStandard();
      assertFenParity(positions.get(0).fen(), oracle.toFEN(), label + " before any move");
      for (int ply = 1; ply < positions.size(); ply++) {
        String san = positions.get(ply).lastMove();
        try {
          oracle = oracle.play(san);
        } catch (RuntimeException e) {
          // Chariot rejecting a token ReplayBoard accepted IS a divergence — label it instead of
          // letting chariot's raw exception (no game/ply context) escape.
          throw new AssertionError(label + ": oracle rejected ply " + ply + " (" + san + ")", e);
        }
        assertFenParity(
            positions.get(ply).fen(),
            oracle.toFEN(),
            label + " after ply " + ply + " (" + san + ")");
      }

      // chess.com's own record of the final position — pins the whole pipeline, tokenizing
      // included, against ground truth this codebase did not compute.
      Matcher currentPosition = CURRENT_POSITION.matcher(pgn);
      assertThat(currentPosition.find()).as("CurrentPosition header in %s", label).isTrue();
      assertThat(positions.get(positions.size() - 1).fen())
          .as("final position vs [CurrentPosition] for %s", label)
          .isEqualTo(currentPosition.group(1));
    }

    // Frozen corpus, so the ply count is a constant; a tokenizer silently dropping or inventing
    // moves moves it even when both boards track each other.
    assertThat(totalPlies).isEqualTo(EXPECTED_TOTAL_PLIES);
  }

  private static void assertFenParity(String actualFen, String oracleFen, String where) {
    String[] actual = actualFen.split(" ");
    String[] oracle = oracleFen.split(" ");
    assertThat(actual[0]).as("placement %s", where).isEqualTo(oracle[0]);
    assertThat(actual[1]).as("side to move %s", where).isEqualTo(oracle[1]);
    assertThat(actual[2]).as("castling rights %s", where).isEqualTo(oracle[2]);
    // Chariot prints an ep square only when an ep capture is actually legal; we print one after
    // every double push (the traditional FEN convention). So an oracle square must always be
    // matched, and only an oracle "-" is inconclusive.
    if (!"-".equals(oracle[3])) {
      assertThat(actual[3]).as("en passant square %s", where).isEqualTo(oracle[3]);
    }
    assertThat(actual[4]).as("halfmove clock %s", where).isEqualTo(oracle[4]);
    assertThat(actual[5]).as("fullmove number %s", where).isEqualTo(oracle[5]);
  }

  private static List<String> loadCorpus() throws IOException {
    try (InputStream in = HikaruCorpusParityTest.class.getResourceAsStream("/hikaru_corpus.pgn")) {
      assertThat(in).as("hikaru_corpus.pgn on test classpath").isNotNull();
      String all = new String(in.readAllBytes(), StandardCharsets.UTF_8);
      return List.of(all.split("(?m)(?=^\\[Event \")")).stream().filter(g -> !g.isBlank()).toList();
    }
  }

  private static String gameLabel(String pgn, int index) {
    Matcher m = LINK.matcher(pgn);
    return "game " + index + (m.find() ? " (" + m.group(1) + ")" : "");
  }
}
