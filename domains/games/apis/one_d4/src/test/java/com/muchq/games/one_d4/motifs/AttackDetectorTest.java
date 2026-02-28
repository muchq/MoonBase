package com.muchq.games.one_d4.motifs;

import static org.assertj.core.api.Assertions.assertThat;

import com.muchq.games.one_d4.engine.model.GameFeatures;
import com.muchq.games.one_d4.engine.model.Motif;
import com.muchq.games.one_d4.engine.model.PositionContext;
import java.util.List;
import org.junit.Test;

public class AttackDetectorTest {

  private final AttackDetector detector = new AttackDetector();

  @Test
  public void motifType() {
    assertThat(detector.motif()).isEqualTo(Motif.ATTACK);
  }

  @Test
  public void detect_directAttackOnKing_emitsOccurrence() {
    // White queen moves from a1 to d5, directly attacking black king on d8.
    String beforeFen = "3k4/8/8/8/8/8/8/Q3K3 w - - 0 10";
    String afterFen = "3k4/8/8/3Q4/8/8/8/4K3 b - - 1 10";
    PositionContext before = new PositionContext(10, beforeFen, true, null);
    PositionContext after = new PositionContext(10, afterFen, false, "Qd5+");

    List<GameFeatures.MotifOccurrence> occurrences = detector.detect(List.of(before, after));

    assertThat(occurrences).isNotEmpty();
    GameFeatures.MotifOccurrence occ =
        occurrences.stream().filter(o -> "kd8".equals(o.target())).findFirst().orElse(null);
    assertThat(occ).isNotNull();
    assertThat(occ.moveNumber()).isEqualTo(10);
    assertThat(occ.side()).isEqualTo("white");
    assertThat(occ.isDiscovered()).isFalse();
    assertThat(occ.isMate()).isFalse();
  }

  @Test
  public void detect_directCheckmate_isMateTrue() {
    // White rook moves from a1 to a8, delivering back-rank checkmate.
    // Black king on g8 is trapped by its own pawns on f7/g7/h7; Ra8 covers the entire 8th rank.
    String beforeFen = "6k1/5ppp/8/8/8/8/8/R5K1 w - - 0 20";
    String afterFen = "R5k1/5ppp/8/8/8/8/8/6K1 b - - 1 20";
    PositionContext before = new PositionContext(20, beforeFen, true, null);
    PositionContext after = new PositionContext(20, afterFen, false, "Ra8#");

    List<GameFeatures.MotifOccurrence> occurrences = detector.detect(List.of(before, after));

    assertThat(occurrences).isNotEmpty();
    GameFeatures.MotifOccurrence mateOcc =
        occurrences.stream().filter(GameFeatures.MotifOccurrence::isMate).findFirst().orElse(null);
    assertThat(mateOcc).isNotNull();
    assertThat(mateOcc.isMate()).isTrue();
    assertThat(mateOcc.isDiscovered()).isFalse();
    assertThat(mateOcc.moveNumber()).isEqualTo(20);
    assertThat(mateOcc.side()).isEqualTo("white");
  }

  @Test
  public void detect_knightFork_emitsMultipleOccurrences() {
    // White knight moves to e5, forking black queen on d7 and black king on f7.
    String beforeFen = "8/3q1k2/8/8/2N5/8/8/4K3 w - - 0 5";
    String afterFen = "8/3q1k2/8/4N3/8/8/8/4K3 b - - 1 5";
    PositionContext before = new PositionContext(5, beforeFen, true, null);
    PositionContext after = new PositionContext(5, afterFen, false, "Ne5");

    List<GameFeatures.MotifOccurrence> occurrences = detector.detect(List.of(before, after));

    // Both the king and queen targets should be emitted
    assertThat(occurrences.stream().map(GameFeatures.MotifOccurrence::target))
        .contains("kf7", "qd7");
    // All should be direct (not discovered)
    assertThat(occurrences).allMatch(o -> !o.isDiscovered());
  }

  @Test
  public void detect_discoveredAttack_emitsIsDiscoveredTrue() {
    // White bishop on e4 moves to h7, revealing white rook on e1 attacking black queen on e8.
    String beforeFen = "4q3/8/8/8/4B3/8/8/4R2K w - - 0 8";
    String afterFen = "4q3/7B/8/8/8/8/8/4R2K b - - 1 8";
    PositionContext before = new PositionContext(8, beforeFen, true, null);
    PositionContext after = new PositionContext(8, afterFen, false, "Bh7");

    List<GameFeatures.MotifOccurrence> occurrences = detector.detect(List.of(before, after));

    assertThat(occurrences).isNotEmpty();
    GameFeatures.MotifOccurrence discovered =
        occurrences.stream()
            .filter(GameFeatures.MotifOccurrence::isDiscovered)
            .findFirst()
            .orElse(null);
    assertThat(discovered).isNotNull();
    assertThat(discovered.isDiscovered()).isTrue();
    assertThat(discovered.isMate()).isFalse();
    assertThat(discovered.movedPiece()).isEqualTo("Be4h7");
    assertThat(discovered.attacker()).isEqualTo("Re1");
    assertThat(discovered.target()).isEqualTo("qe8");
  }

  @Test
  public void detect_noSignificantAttack_noOccurrences() {
    // White pawn moves from e2 to e4, no attack on king/queen/fork.
    String beforeFen = "r1bqkbnr/pppppppp/2n5/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1";
    String afterFen = "r1bqkbnr/pppppppp/2n5/8/4P3/8/PPPP1PPP/RNBQKBNR b KQkq - 0 1";
    PositionContext before = new PositionContext(1, beforeFen, true, null);
    PositionContext after = new PositionContext(1, afterFen, false, "e4");

    List<GameFeatures.MotifOccurrence> occurrences = detector.detect(List.of(before, after));

    assertThat(occurrences).isEmpty();
  }

  @Test
  public void detect_singlePosition_noOccurrences() {
    PositionContext p = new PositionContext(1, "4k3/8/8/8/8/8/8/4K3 w - - 0 1", true, null);
    assertThat(detector.detect(List.of(p))).isEmpty();
  }

  @Test
  public void detect_emptyPositions_noOccurrences() {
    assertThat(detector.detect(List.of())).isEmpty();
  }
}
