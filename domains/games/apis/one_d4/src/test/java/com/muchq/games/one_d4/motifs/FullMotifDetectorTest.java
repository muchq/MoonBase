package com.muchq.games.one_d4.motifs;

import static org.assertj.core.api.Assertions.assertThat;
import static org.assertj.core.api.Assertions.tuple;

import com.muchq.games.one_d4.engine.FeatureExtractor;
import com.muchq.games.one_d4.engine.GameReplayer;
import com.muchq.games.one_d4.engine.PgnParser;
import com.muchq.games.one_d4.engine.model.GameFeatures;
import com.muchq.games.one_d4.engine.model.Motif;
import java.util.List;
import java.util.Set;
import org.junit.Before;
import org.junit.Test;

/**
 * Integration test that runs all motif detectors against a real game PGN and verifies the exact set
 * of detected motifs and their occurrence counts.
 *
 * <p>Game: _prior (1508) vs zapblast (912), 2024-12-30, King's Gambit (C30), 1-0 by checkmate.
 * White launches a king-side attack, wins material through a long series of checks, trades down to
 * K+R+passed-pawn vs K+Q, promotes, and delivers Ra5#.
 */
public class FullMotifDetectorTest {

  private static final String PGN =
      """
      [Event "Live Chess"]
      [Site "Chess.com"]
      [Date "2024.12.30"]
      [Round "-"]
      [White "_prior"]
      [Black "zapblast"]
      [Result "1-0"]
      [ECO "C30"]
      [WhiteElo "1508"]
      [BlackElo "912"]
      [TimeControl "600+5"]
      [Termination "_prior won by checkmate"]

      1. e4 e5 2. f4 d6 3. Nf3 Nc6 4. Bb5 Bd7 5. Nc3 f6 6. f5 Be7 7. Nh4 h5 \
      8. Ng6 Rh6 9. Nd5 Nd4 10. Bxd7+ Qxd7 11. d3 Rh7 12. h4 c6 13. Ngxe7 Nxe7 \
      14. Nxe7 Kxe7 15. Be3 c5 16. g4 hxg4 17. Qxg4 Qa4 18. Bxd4 cxd4 19. Qg6 Rah8 \
      20. a3 Qxc2 21. O-O Rxh4 22. Qxg7+ Ke8 23. Qg6+ Kf8 24. Qxf6+ Ke8 25. Qe6+ Kd8 \
      26. Qxd6+ Kc8 27. Qe6+ Kb8 28. Qxe5+ Ka8 29. Rf2 Rh1+ 30. Kg2 R8h2+ 31. Qxh2 Rxh2+ \
      32. Kxh2 Qxf2+ 33. Kh1 Qxb2 34. Rg1 a6 35. f6 Qf2 36. e5 Qf3+ 37. Kh2 Qf4+ \
      38. Rg3 Qxe5 39. f7 Qh5+ 40. Kg2 Qxf7 41. Rf3 Qa2+ 42. Kg3 Qxa3 43. Kf4 Qf8+ \
      44. Ke4 Qe8+ 45. Kxd4 Qd7+ 46. Ke5 a5 47. d4 a4 48. d5 Qg7+ 49. Ke6 Qg4+ \
      50. Rf5 a3 51. d6 Kb8 52. d7 Qg7 53. d8=Q+ Ka7 54. Ra5# 1-0
      """;

  private FeatureExtractor extractor;

  @Before
  public void setUp() {
    List<MotifDetector> detectors =
        List.of(
            new PinDetector(),
            new CrossPinDetector(),
            new ForkDetector(),
            new SkewerDetector(),
            new DiscoveredAttackDetector(),
            new DiscoveredCheckDetector(),
            new CheckDetector(),
            new CheckmateDetector(),
            new PromotionDetector(),
            new PromotionWithCheckDetector(),
            new PromotionWithCheckmateDetector(),
            new BackRankMateDetector(),
            new SmotheredMateDetector(),
            new SacrificeDetector(),
            new ZugzwangDetector(),
            new DoubleCheckDetector(),
            new InterferenceDetector(),
            new OverloadedPieceDetector());
    extractor = new FeatureExtractor(new PgnParser(), new GameReplayer(), detectors);
  }

  @Test
  public void extractFeatures_returnsCorrectMoveCount() {
    GameFeatures features = extractor.extract(PGN);
    assertThat(features.numMoves()).isEqualTo(54);
  }

  @Test
  public void extractFeatures_detectsExactMotifSet() {
    GameFeatures features = extractor.extract(PGN);

    assertThat(features.motifs())
        .containsExactlyInAnyOrder(
            Motif.PIN,
            Motif.FORK,
            Motif.SKEWER,
            Motif.DISCOVERED_ATTACK,
            Motif.DISCOVERED_CHECK,
            Motif.CHECK,
            Motif.CHECKMATE,
            Motif.PROMOTION,
            Motif.PROMOTION_WITH_CHECK,
            Motif.SACRIFICE,
            Motif.INTERFERENCE,
            Motif.OVERLOADED_PIECE);
  }

  @Test
  public void extractFeatures_motifsNotPresent() {
    GameFeatures features = extractor.extract(PGN);

    Set<Motif> absent =
        Set.of(
            Motif.CROSS_PIN,
            Motif.PROMOTION_WITH_CHECKMATE,
            Motif.BACK_RANK_MATE,
            Motif.SMOTHERED_MATE,
            Motif.ZUGZWANG,
            Motif.DOUBLE_CHECK);
    for (Motif m : absent) {
      assertThat(features.hasMotif(m)).as("expected %s absent", m).isFalse();
    }
  }

  @Test
  public void extractFeatures_check_occurrences() {
    GameFeatures features = extractor.extract(PGN);
    // 10.Bxd7+, 22.Qxg7+, 23.Qg6+, 24.Qxf6+, 25.Qe6+, 26.Qxd6+, 27.Qe6+, 28.Qxe5+,
    // 29...Rh1+, 30...R8h2+, 31...Rxh2+, 32...Qxf2+, 36...Qf3+, 37...Qf4+, 39...Qh5+,
    // 41...Qa2+, 43...Qf8+, 44...Qe8+, 45...Qd7+, 48...Qg7+, 49...Qg4+, 53.d8=Q+, 54.Ra5#
    assertThat(features.occurrences().get(Motif.CHECK))
        .extracting(GameFeatures.MotifOccurrence::moveNumber, GameFeatures.MotifOccurrence::side)
        .containsExactly(
            tuple(10, "white"),
            tuple(22, "white"),
            tuple(23, "white"),
            tuple(24, "white"),
            tuple(25, "white"),
            tuple(26, "white"),
            tuple(27, "white"),
            tuple(28, "white"),
            tuple(29, "black"),
            tuple(30, "black"),
            tuple(31, "black"),
            tuple(32, "black"),
            tuple(36, "black"),
            tuple(37, "black"),
            tuple(39, "black"),
            tuple(41, "black"),
            tuple(43, "black"),
            tuple(44, "black"),
            tuple(45, "black"),
            tuple(48, "black"),
            tuple(49, "black"),
            tuple(53, "white"),
            tuple(54, "white"));
  }

  @Test
  public void extractFeatures_checkmate_Ra5() {
    GameFeatures features = extractor.extract(PGN);
    assertThat(features.occurrences().get(Motif.CHECKMATE))
        .extracting(GameFeatures.MotifOccurrence::moveNumber, GameFeatures.MotifOccurrence::side)
        .containsExactly(tuple(54, "white"));
  }

  @Test
  public void extractFeatures_promotion_d8Q() {
    GameFeatures features = extractor.extract(PGN);
    // 53. d8=Q+
    assertThat(features.occurrences().get(Motif.PROMOTION))
        .extracting(GameFeatures.MotifOccurrence::moveNumber, GameFeatures.MotifOccurrence::side)
        .containsExactly(tuple(53, "white"));
  }

  @Test
  public void extractFeatures_promotionWithCheck_d8QCheck() {
    GameFeatures features = extractor.extract(PGN);
    // 53. d8=Q+ — the promoted queen delivers the check
    assertThat(features.occurrences().get(Motif.PROMOTION_WITH_CHECK))
        .extracting(GameFeatures.MotifOccurrence::moveNumber, GameFeatures.MotifOccurrence::side)
        .containsExactly(tuple(53, "white"));
  }

  @Test
  public void extractFeatures_pin_occurrences() {
    GameFeatures features = extractor.extract(PGN);
    // 4.Bb5 pins Nc6 to the king, plus queen/rook pins in the endgame
    assertThat(features.occurrences().get(Motif.PIN))
        .extracting(GameFeatures.MotifOccurrence::moveNumber, GameFeatures.MotifOccurrence::side)
        .containsExactly(
            tuple(4, "white"),
            tuple(30, "black"),
            tuple(31, "black"),
            tuple(38, "black"),
            tuple(50, "black"),
            tuple(51, "black"));
  }

  @Test
  public void extractFeatures_fork_occurrences() {
    GameFeatures features = extractor.extract(PGN);
    // 8.Ng6 forks Rh6+Ke8, queen forks on 22/24/28 (white), 49...Qg4+ (black)
    assertThat(features.occurrences().get(Motif.FORK))
        .extracting(GameFeatures.MotifOccurrence::moveNumber, GameFeatures.MotifOccurrence::side)
        .containsExactly(
            tuple(8, "white"),
            tuple(22, "white"),
            tuple(24, "white"),
            tuple(28, "white"),
            tuple(49, "black"));
  }

  @Test
  public void extractFeatures_skewer_occurrences() {
    GameFeatures features = extractor.extract(PGN);
    // Rook/queen skewers in the endgame — all by black
    assertThat(features.occurrences().get(Motif.SKEWER))
        .extracting(GameFeatures.MotifOccurrence::moveNumber, GameFeatures.MotifOccurrence::side)
        .containsExactly(
            tuple(29, "black"), tuple(30, "black"), tuple(31, "black"), tuple(43, "black"));
  }

  @Test
  public void extractFeatures_discoveredAttack_occurrences() {
    GameFeatures features = extractor.extract(PGN);
    // 9...Nd4 (nc6 vacates, bd7 attacks Bb5), 11.d3 (Pd2 vacates, Bc1 attacks rh6),
    // 16...hxg4 (ph5 vacates, rh7 attacks Ph4), 30.Kg2 (king vacates, Ra1 attacks rh1),
    // 32...Qxf2+ (qc2 vacates, queen self-reveals and attacks Pb2),
    // 38...Qxe5 (qf4 vacates, queen self-reveals and attacks Rg3),
    // 44.Ke4 (Kf4 vacates, Rf3 attacks qf8)
    assertThat(features.occurrences().get(Motif.DISCOVERED_ATTACK))
        .extracting(
            GameFeatures.MotifOccurrence::moveNumber,
            GameFeatures.MotifOccurrence::side,
            GameFeatures.MotifOccurrence::movedPiece,
            GameFeatures.MotifOccurrence::attacker,
            GameFeatures.MotifOccurrence::target)
        .containsExactly(
            tuple(9, "black", "nc6d4", "bd7", "Bb5"),
            tuple(11, "white", "Pd2d3", "Bc1", "rh6"),
            tuple(16, "black", "ph5g4", "rh7", "Ph4"),
            tuple(30, "white", "Kg1g2", "Ra1", "rh1"),
            tuple(32, "black", "qc2f2", "qf2", "Pb2"),
            tuple(38, "black", "qf4e5", "qe5", "Rg3"),
            tuple(44, "white", "Kf4e4", "Rf3", "qf8"));
  }

  @Test
  public void extractFeatures_discoveredCheck_occurrences() {
    GameFeatures features = extractor.extract(PGN);
    // 32...Qxf2+ — queen moves to f2 (check), revealing a discovered attack on Pb2
    assertThat(features.occurrences().get(Motif.DISCOVERED_CHECK))
        .extracting(
            GameFeatures.MotifOccurrence::moveNumber,
            GameFeatures.MotifOccurrence::side,
            GameFeatures.MotifOccurrence::movedPiece,
            GameFeatures.MotifOccurrence::attacker,
            GameFeatures.MotifOccurrence::target)
        .containsExactly(tuple(32, "black", "qc2f2", "qf2", "Pb2"));
  }

  @Test
  public void extractFeatures_sacrifice_occurrences() {
    GameFeatures features = extractor.extract(PGN);
    assertThat(features.occurrences().get(Motif.SACRIFICE))
        .extracting(GameFeatures.MotifOccurrence::moveNumber, GameFeatures.MotifOccurrence::side)
        .containsExactly(
            tuple(10, "black"),
            tuple(14, "black"),
            tuple(17, "white"),
            tuple(18, "white"),
            tuple(20, "black"),
            tuple(21, "black"),
            tuple(22, "white"),
            tuple(24, "white"),
            tuple(26, "white"),
            tuple(28, "white"),
            tuple(31, "white"),
            tuple(32, "white"),
            tuple(32, "black"),
            tuple(33, "black"),
            tuple(38, "black"),
            tuple(40, "black"),
            tuple(42, "black"),
            tuple(45, "white"));
  }

  @Test
  public void extractFeatures_interference_occurrences() {
    GameFeatures features = extractor.extract(PGN);
    // All interference occurrences are by white
    assertThat(features.occurrences().get(Motif.INTERFERENCE))
        .extracting(GameFeatures.MotifOccurrence::moveNumber, GameFeatures.MotifOccurrence::side)
        .containsExactly(
            tuple(6, "white"),
            tuple(20, "white"),
            tuple(29, "white"),
            tuple(38, "white"),
            tuple(41, "white"),
            tuple(47, "white"),
            tuple(48, "white"),
            tuple(50, "white"));
  }

  @Test
  public void extractFeatures_overloadedPiece_occurrences() {
    GameFeatures features = extractor.extract(PGN);
    assertThat(features.occurrences().get(Motif.OVERLOADED_PIECE))
        .extracting(GameFeatures.MotifOccurrence::moveNumber, GameFeatures.MotifOccurrence::side)
        .containsExactly(
            tuple(8, "white"),
            tuple(9, "white"),
            tuple(10, "white"),
            tuple(11, "white"),
            tuple(12, "white"),
            tuple(12, "black"),
            tuple(13, "black"),
            tuple(17, "black"),
            tuple(18, "white"),
            tuple(24, "white"));
  }

  @Test
  public void extractFeatures_allDetectedMotifsHaveOccurrences() {
    GameFeatures features = extractor.extract(PGN);
    for (Motif motif : features.motifs()) {
      assertThat(features.occurrences()).containsKey(motif);
      assertThat(features.occurrences().get(motif)).isNotEmpty();
    }
  }

  @Test
  public void extractFeatures_allOccurrencesHaveValidFields() {
    GameFeatures features = extractor.extract(PGN);
    for (var entry : features.occurrences().entrySet()) {
      for (GameFeatures.MotifOccurrence occ : entry.getValue()) {
        assertThat(occ.moveNumber()).isPositive();
        assertThat(occ.ply()).isPositive();
        assertThat(occ.side()).isIn("white", "black");
        assertThat(occ.description()).isNotBlank();
      }
    }
  }
}
