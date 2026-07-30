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
 * against chariot at every ply. This is the wide-net complement to GameReplayerParityTest's
 * hand-picked edge cases: real PGNs with clock comments, every disambiguation and castling shape
 * that ~36k plies of top-level play produce.
 *
 * <p>The oracle replays the SANs recorded in {@link PositionContext}, i.e. exactly the tokens the
 * production replayer played, so the comparison isolates board mechanics from PGN tokenizing.
 */
public class HikaruCorpusParityTest {

  private static final Pattern LINK = Pattern.compile("\\[Link \"([^\"]+)\"\\]");

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
        oracle = oracle.play(san);
        assertFenParity(
            positions.get(ply).fen(),
            oracle.toFEN(),
            label + " after ply " + ply + " (" + san + ")");
      }
    }

    // The corpus should exercise real volume; a tokenizing regression that silently dropped moves
    // would show up here long before any single-game assertion.
    assertThat(totalPlies).isGreaterThan(20_000);
  }

  private static void assertFenParity(String actualFen, String oracleFen, String where) {
    String[] actual = actualFen.split(" ");
    String[] oracle = oracleFen.split(" ");
    assertThat(actual[0]).as("placement %s", where).isEqualTo(oracle[0]);
    assertThat(actual[1]).as("side to move %s", where).isEqualTo(oracle[1]);
    assertThat(actual[2]).as("castling rights %s", where).isEqualTo(oracle[2]);
    // Chariot only prints an ep square when an ep capture is actually possible; we print it after
    // every double push (the traditional FEN convention). Only compare when both print a square.
    if (!"-".equals(actual[3]) && !"-".equals(oracle[3])) {
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
