package com.muchq.games.one_d4.motifs;

import static org.assertj.core.api.Assertions.assertThat;

import com.muchq.games.one_d4.engine.model.AttackOccurrence;
import com.muchq.games.one_d4.engine.model.PositionContext;
import java.util.List;
import org.junit.Test;

public class AttackOccurrenceDetectorTest {

  private final AttackOccurrenceDetector detector = new AttackOccurrenceDetector();

  // ── Direct check ──────────────────────────────────────────────────────────

  @Test
  public void directCheck_queenDeliversCheck() {
    // White queen on d1 moves to h5, giving check to black king on e8.
    // Position before: 4k3/8/8/8/8/8/8/3QK3 w - - 0 1
    // Position after:  4k3/8/8/7Q/8/8/8/4K3 b - - 1 1
    String beforeFen = "4k3/8/8/8/8/8/8/3QK3 w - - 0 1";
    String afterFen = "4k3/8/8/7Q/8/8/8/4K3 b - - 1 1";
    PositionContext before = new PositionContext(1, beforeFen, true, null);
    PositionContext after = new PositionContext(1, afterFen, false, "Qh5+");

    List<AttackOccurrence> attacks = detector.detect(List.of(before, after));

    assertThat(attacks).hasSize(1);
    AttackOccurrence a = attacks.get(0);
    assertThat(a.moveNumber()).isEqualTo(1);
    assertThat(a.side()).isEqualTo("white");
    assertThat(a.attacked()).startsWith("k");
    assertThat(a.isCheckmate()).isFalse();
    // direct attack: pieceMoved == attacker (both use destination notation)
    assertThat(a.pieceMoved()).isEqualTo(a.attacker());
    assertThat(a.attacker()).startsWith("Q");
  }

  // ── Checkmate ─────────────────────────────────────────────────────────────

  @Test
  public void checkmate_flaggedOnKingAttack() {
    // Scholar's mate final position: Qxf7#
    // Before: r1bqkb1r/pppp1ppp/2n2n2/4p3/2B1P3/5Q2/PPPP1PPP/RNB1K1NR w KQkq - 4 4
    // After:  r1bqkb1r/ppppQppp/2n2n2/4p3/2B1P3/8/PPPP1PPP/RNB1K1NR b KQkq - 0 4
    String beforeFen = "r1bqkb1r/pppp1ppp/2n2n2/4p3/2B1P3/5Q2/PPPP1PPP/RNB1K1NR w KQkq - 4 4";
    String afterFen = "r1bqkb1r/ppppQppp/2n2n2/4p3/2B1P3/8/PPPP1PPP/RNB1K1NR b KQkq - 0 4";
    PositionContext before = new PositionContext(4, beforeFen, true, null);
    PositionContext after = new PositionContext(4, afterFen, false, "Qxf7#");

    List<AttackOccurrence> attacks = detector.detect(List.of(before, after));

    assertThat(attacks).isNotEmpty();
    // At least one row must target the king with isCheckmate = true
    assertThat(attacks)
        .anyMatch(a -> AttackOccurrenceDetector.isKing(a.attacked()) && a.isCheckmate());
  }

  // ── Knight fork ───────────────────────────────────────────────────────────

  @Test
  public void fork_knightAttacksTwoValuablePieces() {
    // White knight on e5 forks black queen on c4 and black rook on g4.
    // 8/8/8/4N3/2q3r1/8/8/4K2k w - - 0 1
    String beforeFen = "8/8/8/8/2q3r1/8/3N4/4K2k w - - 0 1";
    String afterFen = "8/8/8/4N3/2q3r1/8/8/4K2k b - - 1 1";
    PositionContext before = new PositionContext(5, beforeFen, true, null);
    PositionContext after = new PositionContext(5, afterFen, false, "Ne5");

    List<AttackOccurrence> attacks = detector.detect(List.of(before, after));

    // Both targets should be emitted (queen and rook = fork)
    assertThat(attacks).hasSize(2);
    assertThat(attacks.stream().map(AttackOccurrence::attacked))
        .containsExactlyInAnyOrder("qc4", "rg4");
    // Both share same attacker
    assertThat(attacks).allMatch(a -> a.attacker().startsWith("N"));
  }

  // ── Queen attack (no fork, just queen target) ────────────────────────────

  @Test
  public void queenAttack_emittedWithoutFork() {
    // White rook on a1 moves to a4, now on same rank as black queen on d4. No fork.
    String beforeFen = "7k/8/8/8/3q4/8/8/R3K3 w - - 0 1";
    String afterFen = "7k/8/8/8/R2q4/8/8/4K3 b - - 1 1";
    PositionContext before = new PositionContext(3, beforeFen, true, null);
    PositionContext after = new PositionContext(3, afterFen, false, "Ra4");

    List<AttackOccurrence> attacks = detector.detect(List.of(before, after));

    assertThat(attacks).hasSize(1);
    assertThat(attacks.get(0).attacked()).startsWith("q");
  }

  // ── No significant attack ─────────────────────────────────────────────────

  @Test
  public void noSignificantAttack_returnsEmpty() {
    // Pawn push e4, attacking nothing significant
    String beforeFen = "r1bqkbnr/pppppppp/2n5/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1";
    String afterFen = "r1bqkbnr/pppppppp/2n5/8/4P3/8/PPPP1PPP/RNBQKBNR b KQkq e3 0 1";
    PositionContext before = new PositionContext(1, beforeFen, true, null);
    PositionContext after = new PositionContext(1, afterFen, false, "e4");

    List<AttackOccurrence> attacks = detector.detect(List.of(before, after));

    assertThat(attacks).isEmpty();
  }

  // ── Discovered check ──────────────────────────────────────────────────────

  @Test
  public void discoveredCheck_emitsAttackRowWithDifferentMovedAndAttacker() {
    // White bishop on e4 moves off the e-file, revealing white rook on e1 checking black king e8.
    String beforeFen = "4k3/8/8/8/4B3/8/8/4RK2 w - - 0 1";
    String afterFen = "4k3/7B/8/8/8/8/8/4RK2 b - - 1 1";
    PositionContext before = new PositionContext(6, beforeFen, true, null);
    PositionContext after = new PositionContext(6, afterFen, false, "Bh7+");

    List<AttackOccurrence> attacks = detector.detect(List.of(before, after));

    // Discovered check attack on king
    assertThat(attacks).isNotEmpty();
    AttackOccurrence discoveredCheck =
        attacks.stream()
            .filter(a -> AttackOccurrenceDetector.isKing(a.attacked()))
            .filter(a -> !a.pieceMoved().equals(a.attacker()))
            .findFirst()
            .orElse(null);
    assertThat(discoveredCheck).isNotNull();
    assertThat(discoveredCheck.attacked()).startsWith("k");
    assertThat(discoveredCheck.attacker()).startsWith("R");
  }
}
