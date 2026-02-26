package com.muchq.games.one_d4.motifs;

import static org.assertj.core.api.Assertions.assertThat;

import com.muchq.games.one_d4.engine.model.GameFeatures;
import com.muchq.games.one_d4.engine.model.Motif;
import com.muchq.games.one_d4.engine.model.PositionContext;
import java.util.List;
import org.junit.Test;

public class DiscoveredAttackDetectorTest {

  private final DiscoveredAttackDetector detector = new DiscoveredAttackDetector();

  @Test
  public void motifType() {
    assertThat(detector.motif()).isEqualTo(Motif.DISCOVERED_ATTACK);
  }

  @Test
  public void discoveredAttack_rookRevealsOnQueen() {
    // White bishop on e4 blocks white rook on e1 from black queen on e8.
    // Bishop moves to h7, revealing rook attack on queen.
    String beforeFen = "4q3/8/8/8/4B3/8/8/4R3 w - - 0 1";
    String afterFen = "4q3/7B/8/8/8/8/8/4R3 b - - 1 1";
    PositionContext before = new PositionContext(10, beforeFen, true, null);
    PositionContext after = new PositionContext(10, afterFen, false, "Bh7");

    List<GameFeatures.MotifOccurrence> occurrences = detector.detect(List.of(before, after));

    assertThat(occurrences).hasSize(1);
    GameFeatures.MotifOccurrence occ = occurrences.get(0);
    assertThat(occ.moveNumber()).isEqualTo(10);
    assertThat(occ.side()).isEqualTo("white");
    assertThat(occ.movedPiece()).isEqualTo("Be4->h7");
    assertThat(occ.attacker()).isEqualTo("Re1");
    assertThat(occ.target()).isEqualTo("qe8");
  }

  @Test
  public void discoveredAttack_bishopRevealsOnRook() {
    // White knight on d5 blocks white bishop on a2 from black rook on f7.
    // Knight moves to f4, revealing bishop attack on rook.
    String beforeFen = "8/5r2/8/3N4/8/8/B7/4K3 w - - 0 1";
    String afterFen = "8/5r2/8/8/5N2/8/B7/4K3 b - - 1 1";
    PositionContext before = new PositionContext(8, beforeFen, true, null);
    PositionContext after = new PositionContext(8, afterFen, false, "Nf4");

    List<GameFeatures.MotifOccurrence> occurrences = detector.detect(List.of(before, after));

    assertThat(occurrences).hasSize(1);
    GameFeatures.MotifOccurrence occ = occurrences.get(0);
    assertThat(occ.movedPiece()).isEqualTo("Nd5->f4");
    assertThat(occ.attacker()).isEqualTo("Ba2");
    assertThat(occ.target()).isEqualTo("rf7");
  }

  @Test
  public void discoveredAttack_onPawn() {
    // White rook on e1, white knight on e4, black pawn on e7.
    // Knight moves off e-file, revealing rook attack on pawn.
    String beforeFen = "4k3/4p3/8/8/4N3/8/8/4R2K w - - 0 1";
    String afterFen = "4k3/4p3/8/8/8/2N5/8/4R2K b - - 1 1";
    PositionContext before = new PositionContext(5, beforeFen, true, null);
    PositionContext after = new PositionContext(5, afterFen, false, "Nc3");

    List<GameFeatures.MotifOccurrence> occurrences = detector.detect(List.of(before, after));

    assertThat(occurrences).hasSize(1);
    GameFeatures.MotifOccurrence occ = occurrences.get(0);
    assertThat(occ.movedPiece()).isEqualTo("Ne4->c3");
    assertThat(occ.attacker()).isEqualTo("Re1");
    assertThat(occ.target()).isEqualTo("pe7");
  }

  @Test
  public void discoveredAttack_onKing_isDiscoveredCheck() {
    // White rook on e1, white bishop on e4, black king on e8.
    // Bishop moves to h7, revealing rook attack on king (discovered check).
    String beforeFen = "4k3/8/8/8/4B3/8/8/4R3 w - - 0 1";
    String afterFen = "4k3/7B/8/8/8/8/8/4R3 b - - 1 1";
    PositionContext before = new PositionContext(10, beforeFen, true, null);
    PositionContext after = new PositionContext(10, afterFen, false, "Bh7+");

    List<GameFeatures.MotifOccurrence> occurrences = detector.detect(List.of(before, after));

    assertThat(occurrences).hasSize(1);
    GameFeatures.MotifOccurrence occ = occurrences.get(0);
    assertThat(occ.movedPiece()).isEqualTo("Be4->h7");
    assertThat(occ.attacker()).isEqualTo("Re1");
    assertThat(occ.target()).isEqualTo("ke8");
  }

  @Test
  public void discoveredAttack_blackMoving() {
    // Black rook on e8, black bishop on e5, white queen on e1.
    // Black bishop moves to h2, revealing rook attack on white queen.
    String beforeFen = "4r3/8/8/4b3/8/8/8/4Q2K b - - 0 1";
    String afterFen = "4r3/8/8/8/8/8/7b/4Q2K w - - 1 1";
    PositionContext before = new PositionContext(10, beforeFen, false, null);
    PositionContext after = new PositionContext(10, afterFen, true, "Bh2");

    List<GameFeatures.MotifOccurrence> occurrences = detector.detect(List.of(before, after));

    assertThat(occurrences).hasSize(1);
    GameFeatures.MotifOccurrence occ = occurrences.get(0);
    assertThat(occ.side()).isEqualTo("black");
    assertThat(occ.movedPiece()).isEqualTo("be5->h2");
    assertThat(occ.attacker()).isEqualTo("re8");
    assertThat(occ.target()).isEqualTo("Qe1");
  }

  @Test
  public void discoveredAttack_doesNotFireForDirectAttack() {
    // White bishop moves from f3 to d5, directly attacking black rook on c6.
    // No sliding piece revealed behind f3.
    String beforeFen = "8/8/2r5/8/8/5B2/8/4K3 w - - 0 1";
    String afterFen = "8/8/2r5/3B4/8/8/8/4K3 b - - 1 1";
    PositionContext before = new PositionContext(5, beforeFen, true, null);
    PositionContext after = new PositionContext(5, afterFen, false, "Bd5");

    List<GameFeatures.MotifOccurrence> occurrences = detector.detect(List.of(before, after));

    assertThat(occurrences).isEmpty();
  }

  @Test
  public void discoveredAttack_doesNotFireForEqualTrade() {
    // White rook captures black rook — no piece is revealed behind the move.
    String beforeFen = "4k3/8/8/4r3/8/8/8/4R2K w - - 0 1";
    String afterFen = "4k3/8/8/4R3/8/8/8/7K b - - 0 1";
    PositionContext before = new PositionContext(5, beforeFen, true, null);
    PositionContext after = new PositionContext(5, afterFen, false, "Rxe5");

    List<GameFeatures.MotifOccurrence> occurrences = detector.detect(List.of(before, after));

    assertThat(occurrences).isEmpty();
  }

  @Test
  public void discoveredAttack_singlePosition_noDetection() {
    PositionContext p = new PositionContext(1, "4k3/8/8/8/8/8/8/4K3 w - - 0 1", true, null);
    assertThat(detector.detect(List.of(p))).isEmpty();
  }

  @Test
  public void discoveredAttack_emptyPositions() {
    assertThat(detector.detect(List.of())).isEmpty();
  }
}
