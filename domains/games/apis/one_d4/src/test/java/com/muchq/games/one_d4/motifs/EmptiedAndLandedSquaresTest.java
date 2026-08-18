package com.muchq.games.one_d4.motifs;

import static org.assertj.core.api.Assertions.assertThat;

import com.muchq.games.one_d4.engine.model.GameFeatures;
import com.muchq.games.one_d4.engine.model.PositionContext;
import java.util.List;
import org.junit.jupiter.api.Test;

/**
 * Three moves empty or fill a square the notation does not name, and each one hid a class of
 * occurrence for as long as this pipeline has existed. Found by porting it to C++ and reviewing the
 * port (#1389 phase 2); fixed on both sides so the two indexers keep agreeing.
 *
 * <ul>
 *   <li>En passant empties a third square — the taken pawn's, on neither square the move names.
 *   <li>A promotion puts down a piece that is not the one that left, so the new piece was read as
 *       one the pawn had been hiding.
 *   <li>Castling lands a rook on a new file, where it pins, skewers and attacks. "O-O" names no
 *       square, so none of that was recorded.
 * </ul>
 */
public class EmptiedAndLandedSquaresTest {

  private static List<PositionContext> move(
      String beforeFen, String afterFen, boolean whiteToMoveAfter, String san, int moveNumber) {
    return List.of(
        new PositionContext(moveNumber, beforeFen, !whiteToMoveAfter, null),
        new PositionContext(moveNumber, afterFen, whiteToMoveAfter, san));
  }

  @Test
  public void enPassantOpensTheLineThroughTheTakenPawn() {
    // Bg2's line to ra8 was blocked by the pawn on d5; exd6 takes it off.
    List<GameFeatures.MotifOccurrence> occurrences =
        new AttackDetector()
            .detect(
                move(
                    "r6k/8/8/3pP3/8/8/6B1/6K1 w - d6 0 2",
                    "r6k/8/3P4/8/8/8/6B1/6K1 b - - 0 2",
                    false,
                    "exd6",
                    2));

    assertThat(occurrences)
        .anySatisfy(
            occ -> {
              assertThat(occ.isDiscovered()).isTrue();
              assertThat(occ.attacker()).isEqualTo("Bg2");
              assertThat(occ.target()).isEqualTo("ra8");
              assertThat(occ.movedPiece())
                  .as("the pawn that took, not the pawn taken")
                  .isEqualTo("Pe5d6");
            });
  }

  @Test
  public void aPromotedPieceIsNotADiscovery() {
    List<GameFeatures.MotifOccurrence> occurrences =
        new AttackDetector()
            .detect(
                move(
                    "8/4P3/8/8/4r2k/8/8/6K1 w - - 0 1",
                    "4Q3/8/8/8/4r2k/8/8/6K1 b - - 0 1",
                    false,
                    "e8=Q",
                    1));

    assertThat(occurrences).allSatisfy(occ -> assertThat(occ.isDiscovered()).isFalse());
  }

  @Test
  public void aCastledRookAttacks() {
    // 1. O-O# — mate, and until this fix it produced no ATTACK row at all,
    // so the derived CHECKMATE and DOUBLE_CHECK rows had nothing to work from.
    List<GameFeatures.MotifOccurrence> occurrences =
        new AttackDetector()
            .detect(
                move(
                    "5k2/4p1p1/3N4/8/8/8/B7/4K2R w K - 0 1",
                    "5k2/4p1p1/3N4/8/8/8/B7/5RK1 b - - 1 1",
                    false,
                    "O-O#",
                    1));

    assertThat(occurrences)
        .anySatisfy(
            occ -> {
              assertThat(occ.attacker()).isEqualTo("Rf1");
              assertThat(occ.target()).isEqualTo("kf8");
              assertThat(occ.isMate()).isTrue();
              assertThat(occ.isDiscovered()).as("the rook moved; it discovered nothing").isFalse();
            });
  }

  @Test
  public void aCastledRookPins() {
    List<GameFeatures.MotifOccurrence> occurrences =
        new PinDetector()
            .detect(
                move(
                    "5k2/8/5n2/8/8/8/8/4K2R w K - 0 1",
                    "5k2/8/5n2/8/8/8/8/5RK1 b - - 1 1",
                    false,
                    "O-O",
                    1));

    assertThat(occurrences).hasSize(1);
    assertThat(occurrences.get(0).attacker()).isEqualTo("Rf1");
    assertThat(occurrences.get(0).target()).isEqualTo("nf6");
    assertThat(occurrences.get(0).pinType()).isEqualTo("ABSOLUTE");
  }

  @Test
  public void queensideCastlingLandsTheRookOnTheDFile() {
    assertThat(BoardUtils.landedSquares("O-O-O", true))
        .extracting(square -> BoardUtils.squareName(square[0], square[1]))
        .containsExactly("c1", "d1");
    assertThat(BoardUtils.landedSquares("O-O", false))
        .extracting(square -> BoardUtils.squareName(square[0], square[1]))
        .containsExactly("g8", "f8");
  }
}
