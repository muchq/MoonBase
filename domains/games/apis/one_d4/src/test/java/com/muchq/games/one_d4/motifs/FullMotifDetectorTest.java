package com.muchq.games.one_d4.motifs;

import com.muchq.games.one_d4.engine.FeatureExtractor;
import com.muchq.games.one_d4.engine.GameReplayer;
import com.muchq.games.one_d4.engine.PgnParser;
import com.muchq.games.one_d4.engine.model.GameFeatures;
import com.muchq.games.one_d4.engine.model.Motif;
import org.junit.Before;
import org.junit.Test;

import java.util.List;
import java.util.Set;

import static org.assertj.core.api.Assertions.assertThat;

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
  public void extractFeatures_check_23occurrences() {
    GameFeatures features = extractor.extract(PGN);
    // Bxd7+, Qxg7+, Qg6+, Qxf6+, Qe6+, Qxd6+, Qe6+, Qxe5+, Rh1+, R8h2+, Rxh2+,
    // Qxf2+, Qf3+, Qf4+, Qh5+, Qa2+, Qf8+, Qe8+, Qd7+, Qg7+, Qg4+, d8=Q+, Ra5#
    List<GameFeatures.MotifOccurrence> checks = features.occurrences().get(Motif.CHECK);
    assertThat(checks).hasSize(23);
    // First check is 10. Bxd7+ (white)
    assertThat(checks.get(0).moveNumber()).isEqualTo(10);
    assertThat(checks.get(0).side()).isEqualTo("white");
    // Last check is 54. Ra5# (white, the mating move)
    assertThat(checks.get(checks.size() - 1).moveNumber()).isEqualTo(54);
  }

  @Test
  public void extractFeatures_checkmate_Ra5() {
    GameFeatures features = extractor.extract(PGN);
    List<GameFeatures.MotifOccurrence> mates = features.occurrences().get(Motif.CHECKMATE);
    assertThat(mates).hasSize(1);
    assertThat(mates.get(0).moveNumber()).isEqualTo(54);
    assertThat(mates.get(0).side()).isEqualTo("white");
  }

  @Test
  public void extractFeatures_promotion_d8Q() {
    GameFeatures features = extractor.extract(PGN);
    // 53. d8=Q+
    List<GameFeatures.MotifOccurrence> promotions = features.occurrences().get(Motif.PROMOTION);
    assertThat(promotions).hasSize(1);
    assertThat(promotions.get(0).moveNumber()).isEqualTo(53);
    assertThat(promotions.get(0).side()).isEqualTo("white");
  }

  @Test
  public void extractFeatures_promotionWithCheck_d8QCheck() {
    GameFeatures features = extractor.extract(PGN);
    // 53. d8=Q+ — the promoted queen delivers the check
    List<GameFeatures.MotifOccurrence> promChecks =
        features.occurrences().get(Motif.PROMOTION_WITH_CHECK);
    assertThat(promChecks).hasSize(1);
    assertThat(promChecks.get(0).moveNumber()).isEqualTo(53);
    assertThat(promChecks.get(0).side()).isEqualTo("white");
  }

  @Test
  public void extractFeatures_pin_6occurrences() {
    GameFeatures features = extractor.extract(PGN);
    // Bb5 pin (move 4), plus several queen/rook pins in the endgame
    List<GameFeatures.MotifOccurrence> pins = features.occurrences().get(Motif.PIN);
    assertThat(pins).hasSize(6);
    assertThat(pins.get(0).moveNumber()).isEqualTo(4);
    assertThat(pins.get(0).side()).isEqualTo("white");
  }

  @Test
  public void extractFeatures_fork_5occurrences() {
    GameFeatures features = extractor.extract(PGN);
    // Ng6 forking rook+king (move 8), queen forks (moves 22, 24, 28), black queen fork (move 50)
    List<GameFeatures.MotifOccurrence> forks = features.occurrences().get(Motif.FORK);
    assertThat(forks).hasSize(5);
    assertThat(forks.get(0).moveNumber()).isEqualTo(8);
    assertThat(forks.get(0).side()).isEqualTo("white");
  }

  @Test
  public void extractFeatures_skewer_4occurrences() {
    GameFeatures features = extractor.extract(PGN);
    // Rook and queen skewers in the middlegame/endgame
    List<GameFeatures.MotifOccurrence> skewers = features.occurrences().get(Motif.SKEWER);
    assertThat(skewers).hasSize(4);
  }

  @Test
  public void extractFeatures_discoveredAttack_7occurrences() {
    GameFeatures features = extractor.extract(PGN);
    // Various discovered attacks throughout the game from both sides
    List<GameFeatures.MotifOccurrence> discovered =
        features.occurrences().get(Motif.DISCOVERED_ATTACK);
    assertThat(discovered).hasSize(7);
    // First discovered attack: 9...Nd4 — knight vacates c6, revealing black bishop on d7
    // attacking white bishop on b5.
    GameFeatures.MotifOccurrence first = discovered.getFirst();
    assertThat(first.moveNumber()).isEqualTo(9);
    assertThat(first.side()).isEqualTo("black");
    assertThat(first.movedPiece()).isEqualTo("nc6->d4");
    assertThat(first.attacker()).isEqualTo("bd7");
    assertThat(first.target()).isEqualTo("Bb5");
    // All discovered attacks must have structured piece data populated
    for (GameFeatures.MotifOccurrence occ : discovered) {
      assertThat(occ.movedPiece()).as("movedPiece at move %d", occ.moveNumber()).isNotNull();
      assertThat(occ.attacker()).as("attacker at move %d", occ.moveNumber()).isNotNull();
      assertThat(occ.target()).as("target at move %d", occ.moveNumber()).isNotNull();
    }
  }

  @Test
  public void extractFeatures_discoveredCheck_1occurrence() {
    GameFeatures features = extractor.extract(PGN);
    // 33... Qxf2+ is a discovered check — queen moves, revealing check from another piece
    List<GameFeatures.MotifOccurrence> discChecks =
        features.occurrences().get(Motif.DISCOVERED_CHECK);
    assertThat(discChecks).hasSize(1);
    GameFeatures.MotifOccurrence occ = discChecks.get(0);
    assertThat(occ.moveNumber()).isEqualTo(32);
    assertThat(occ.side()).isEqualTo("black");
    // Structured fields must be populated for discovered check
    assertThat(occ.movedPiece()).isNotNull();
    assertThat(occ.attacker()).isNotNull();
    assertThat(occ.target()).isNotNull();
  }

  @Test
  public void extractFeatures_sacrifice_18occurrences() {
    GameFeatures features = extractor.extract(PGN);
    // Many material sacrifices throughout this wild game
    List<GameFeatures.MotifOccurrence> sacrifices = features.occurrences().get(Motif.SACRIFICE);
    assertThat(sacrifices).hasSize(18);
  }

  @Test
  public void extractFeatures_interference_8occurrences() {
    GameFeatures features = extractor.extract(PGN);
    List<GameFeatures.MotifOccurrence> interferences =
        features.occurrences().get(Motif.INTERFERENCE);
    assertThat(interferences).hasSize(8);
    // All interference occurrences are by white
    assertThat(interferences).allMatch(occ -> occ.side().equals("white"));
  }

  @Test
  public void extractFeatures_overloadedPiece_10occurrences() {
    GameFeatures features = extractor.extract(PGN);
    List<GameFeatures.MotifOccurrence> overloaded =
        features.occurrences().get(Motif.OVERLOADED_PIECE);
    assertThat(overloaded).hasSize(10);
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
