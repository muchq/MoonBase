package com.muchq.games.one_d4.engine;

import static org.assertj.core.api.Assertions.assertThat;

import chariot.chess.Board;
import chariot.model.PGN;
import com.muchq.games.one_d4.engine.model.PositionContext;
import java.util.List;
import org.junit.jupiter.api.Test;

/**
 * Replays a corpus of games with both the internal {@link ReplayBoard} (via {@link GameReplayer})
 * and chariot's {@code Board}, asserting FEN parity at every ply. Chariot is the correctness
 * oracle; the internal board exists purely because chariot's immutable play() is too slow for bulk
 * indexing.
 */
public class GameReplayerParityTest {

  private static final String KINGS_GAMBIT =
      "1. e4 e5 2. f4 d6 3. Nf3 Nc6 4. Bb5 Bd7 5. Nc3 f6 6. f5 Be7 7. Nh4 h5 "
          + "8. Ng6 Rh6 9. Nd5 Nd4 10. Bxd7+ Qxd7 11. d3 Rh7 12. h4 c6 13. Ngxe7 Nxe7 "
          + "14. Nxe7 Kxe7 15. Be3 c5 16. g4 hxg4 17. Qxg4 Qa4 18. Bxd4 cxd4 19. Qg6 Rah8 "
          + "20. a3 Qxc2 21. O-O Rxh4 22. Qxg7+ Ke8 23. Qg6+ Kf8 24. Qxf6+ Ke8 25. Qe6+ Kd8 "
          + "26. Qxd6+ Kc8 27. Qe6+ Kb8 28. Qxe5+ Ka8 29. Rf2 Rh1+ 30. Kg2 R8h2+ 31. Qxh2 Rxh2+ "
          + "32. Kxh2 Qxf2+ 33. Kh1 Qxb2 34. Rg1 a6 35. f6 Qf2 36. e5 Qf3+ 37. Kh2 Qf4+ "
          + "38. Rg3 Qxe5 39. f7 Qh5+ 40. Kg2 Qxf7 41. Rf3 Qa2+ 42. Kg3 Qxa3 43. Kf4 Qf8+ "
          + "44. Ke4 Qe8+ 45. Kxd4 Qd7+ 46. Ke5 a5 47. d4 a4 48. d5 Qg7+ 49. Ke6 Qg4+ "
          + "50. Rf5 a3 51. d6 Kb8 52. d7 Qg7 53. d8=Q+ Ka7 54. Ra5# 1-0";

  private static final String EN_PASSANT =
      "1. e4 Nf6 2. e5 d5 3. exd6 exd6 4. Nf3 Be7 5. Be2 O-O 6. O-O";

  private static final String UNDERPROMOTION =
      "1. g4 h5 2. gxh5 g6 3. hxg6 Bh6 4. g7 Be3 5. gxh8=N";

  private static final String QUEENSIDE_CASTLES_AND_ROOK_DISAMBIGUATION =
      "1. d4 d5 2. Nc3 Nc6 3. Bf4 Bf5 4. Qd2 Qd7 5. O-O-O O-O-O 6. Nf3 Nf6 7. e3 e6 "
          + "8. Bd3 Bxd3 9. Qxd3 Bd6 10. Bxd6 Qxd6 11. Rde1 Rde8";

  private static final String KNIGHT_DISAMBIGUATION =
      "1. Nf3 Nc6 2. e3 e5 3. d4 exd4 4. exd4 d5 5. Bd3 Bd6 6. Nbd2 Nce7";

  // Exercises the black-side halves in one line: black en passant capture (dxc3), black
  // capture-underpromotion (bxa1=N), and the a1-corner castling-rights row when the rook falls.
  private static final String BLACK_EP_AND_UNDERPROMOTION =
      "1. Nf3 d5 2. e4 d4 3. c4 dxc3 4. d4 cxb2 5. Bd2 bxa1=N";

  @Test
  public void kingsGambitGameMatchesChariotAtEveryPly() {
    assertParity(KINGS_GAMBIT);
  }

  @Test
  public void enPassantGameMatchesChariotAtEveryPly() {
    assertParity(EN_PASSANT);
  }

  @Test
  public void underpromotionGameMatchesChariotAtEveryPly() {
    assertParity(UNDERPROMOTION);
  }

  @Test
  public void queensideCastlingGameMatchesChariotAtEveryPly() {
    assertParity(QUEENSIDE_CASTLES_AND_ROOK_DISAMBIGUATION);
  }

  @Test
  public void knightDisambiguationGameMatchesChariotAtEveryPly() {
    assertParity(KNIGHT_DISAMBIGUATION);
  }

  @Test
  public void blackEnPassantAndUnderpromotionGameMatchesChariotAtEveryPly() {
    assertParity(BLACK_EP_AND_UNDERPROMOTION);
  }

  private static void assertParity(String moveText) {
    List<PositionContext> positions = new GameReplayer().replay(moveText);
    List<String> sans =
        PGN.Text.parse(moveText)
            .filter(t -> t instanceof PGN.Text.Move)
            .map(t -> ((PGN.Text.Move) t).san())
            .toList();
    assertThat(positions).hasSize(sans.size() + 1);

    Board oracle = Board.ofStandard();
    assertFenParity(positions.get(0).fen(), oracle.toFEN(), "before any move");
    for (int ply = 0; ply < sans.size(); ply++) {
      oracle = oracle.play(sans.get(ply));
      assertFenParity(
          positions.get(ply + 1).fen(),
          oracle.toFEN(),
          "after ply " + (ply + 1) + " (" + sans.get(ply) + ")");
    }
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
}
