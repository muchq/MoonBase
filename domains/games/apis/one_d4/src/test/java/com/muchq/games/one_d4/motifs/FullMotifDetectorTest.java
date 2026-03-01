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
            new SkewerDetector(),
            new AttackDetector(),
            new CheckDetector(),
            new PromotionDetector(),
            new PromotionWithCheckDetector(),
            new PromotionWithCheckmateDetector(),
            new BackRankMateDetector(),
            new SmotheredMateDetector(),
            new ZugzwangDetector(),
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

    // FORK, CHECKMATE, DISCOVERED_CHECK, DOUBLE_CHECK are derived at query/response time from
    // ATTACK rows — they are not materialized at index time.
    assertThat(features.motifs())
        .containsExactlyInAnyOrder(
            Motif.PIN,
            Motif.SKEWER,
            Motif.ATTACK,
            Motif.CHECK,
            Motif.PROMOTION,
            Motif.PROMOTION_WITH_CHECK,
            Motif.OVERLOADED_PIECE);
  }

  @Test
  public void extractFeatures_motifsNotPresent() {
    GameFeatures features = extractor.extract(PGN);

    // CHECKMATE, DISCOVERED_CHECK, DOUBLE_CHECK are now derived at query/response time (not
    // indexed)
    Set<Motif> absent =
        Set.of(
            Motif.CROSS_PIN,
            Motif.DISCOVERED_ATTACK,
            Motif.DISCOVERED_CHECK,
            Motif.CHECKMATE,
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
    List<GameFeatures.MotifOccurrence> pins = features.occurrences().get(Motif.PIN);
    // 4.Bb5 pins Nc6 to the king; endgame queen/rook pins; also relative pins detected
    assertThat(pins).isNotEmpty();

    // Key known absolute pins are present
    assertThat(pins)
        .extracting(GameFeatures.MotifOccurrence::moveNumber, GameFeatures.MotifOccurrence::side)
        .contains(
            tuple(4, "white"),
            tuple(30, "black"),
            tuple(31, "black"),
            tuple(38, "black"),
            tuple(50, "black"),
            tuple(51, "black"));

    // All pin occurrences have required structured fields
    assertThat(pins).allMatch(o -> o.attacker() != null, "attacker non-null");
    assertThat(pins).allMatch(o -> o.target() != null, "target non-null");
    assertThat(pins).allMatch(o -> o.pinType() != null, "pinType non-null");
    assertThat(pins)
        .allMatch(
            o -> o.pinType().equals("ABSOLUTE") || o.pinType().equals("RELATIVE"),
            "pinType is ABSOLUTE or RELATIVE");
    assertThat(pins).allMatch(o -> !o.isDiscovered(), "isDiscovered false");
    assertThat(pins).allMatch(o -> !o.isMate(), "isMate false");
  }

  @Test
  public void extractFeatures_fork_notMaterializedAtIndexTime() {
    // FORK is derived at query/response time from ATTACK rows; extract() must not produce FORK.
    // The underlying ATTACK rows that would constitute forks ARE present.
    GameFeatures features = extractor.extract(PGN);
    assertThat(features.occurrences()).doesNotContainKey(Motif.FORK);
    assertThat(features.hasMotif(Motif.FORK)).isFalse();
    assertThat(features.hasMotif(Motif.ATTACK)).isTrue();
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
  public void extractFeatures_attack_discoveredOccurrences() {
    GameFeatures features = extractor.extract(PGN);
    // Discovered attacks within ATTACK occurrences (isDiscovered=true):
    // 9...Nd4 (nc6 vacates, bd7 attacks Bb5), 11.d3 (Pd2 vacates, Bc1 attacks rh6),
    // 16...hxg4 (ph5 vacates, rh7 attacks Ph4), 30.Kg2 (king vacates, Ra1 attacks rh1),
    // 44.Ke4 (Kf4 vacates, Rf3 attacks qf8)
    // Note: moves 32 and 38 are NOT discovered attacks — the moved piece IS the attacker.
    List<GameFeatures.MotifOccurrence> discoveredAttacks =
        features.occurrences().get(Motif.ATTACK).stream()
            .filter(GameFeatures.MotifOccurrence::isDiscovered)
            .toList();
    assertThat(discoveredAttacks)
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
            tuple(44, "white", "Kf4e4", "Rf3", "qf8"));
  }

  @Test
  public void extractFeatures_discoveredCheck_occurrences() {
    GameFeatures features = extractor.extract(PGN);
    // DISCOVERED_CHECK is derived at query/response time from ATTACK rows; not indexed.
    assertThat(features.occurrences()).doesNotContainKey(Motif.DISCOVERED_CHECK);
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
