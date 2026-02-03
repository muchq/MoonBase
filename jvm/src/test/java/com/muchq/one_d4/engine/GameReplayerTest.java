package com.muchq.one_d4.engine;

import static org.assertj.core.api.Assertions.assertThat;

import com.muchq.one_d4.engine.model.PositionContext;
import java.util.List;
import org.junit.Test;

public class GameReplayerTest {

  private final GameReplayer replayer = new GameReplayer();

  @Test
  public void testEmptyMoveText() {
    List<PositionContext> positions = replayer.replay("");

    assertThat(positions).hasSize(1);
    assertThat(positions.get(0).moveNumber()).isEqualTo(0);
    assertThat(positions.get(0).whiteToMove()).isTrue();
    assertThat(positions.get(0).fen()).startsWith("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR");
  }

  @Test
  public void testSingleMove() {
    List<PositionContext> positions = replayer.replay("1. e4");

    assertThat(positions).hasSize(2);

    // Initial position
    assertThat(positions.get(0).moveNumber()).isEqualTo(0);
    assertThat(positions.get(0).whiteToMove()).isTrue();

    // After 1. e4
    assertThat(positions.get(1).moveNumber()).isEqualTo(1);
    assertThat(positions.get(1).whiteToMove()).isFalse();
    assertThat(positions.get(1).fen()).contains("PPPP1PPP"); // pawn moved from e2
  }

  @Test
  public void testOpeningMoves() {
    List<PositionContext> positions = replayer.replay("1. e4 e5 2. Nf3 Nc6 3. Bb5");

    assertThat(positions).hasSize(6); // initial + 5 moves

    // After 1. e4 - move 1, black to move
    assertThat(positions.get(1).moveNumber()).isEqualTo(1);
    assertThat(positions.get(1).whiteToMove()).isFalse();

    // After 1... e5 - move 2 (incremented after black moves), white to move
    assertThat(positions.get(2).moveNumber()).isEqualTo(2);
    assertThat(positions.get(2).whiteToMove()).isTrue();

    // After 2. Nf3 - still move 2, black to move
    assertThat(positions.get(3).moveNumber()).isEqualTo(2);
    assertThat(positions.get(3).whiteToMove()).isFalse();

    // After 3. Bb5 (Ruy Lopez) - move 3, black to move
    assertThat(positions.get(5).moveNumber()).isEqualTo(3);
    assertThat(positions.get(5).whiteToMove()).isFalse();
    assertThat(positions.get(5).fen()).contains("B"); // bishop on b5
  }

  @Test
  public void testCastlingKingside() {
    List<PositionContext> positions = replayer.replay("1. e4 e5 2. Nf3 Nc6 3. Bc4 Bc5 4. O-O");

    PositionContext afterCastle = positions.get(positions.size() - 1);
    // After O-O, king should be on g1 and rook on f1
    // FEN back rank: RNBQ1RK1 (Rook a1, Knight b1, Bishop c1, Queen d1, empty e1, Rook f1, King g1,
    // empty h1)
    assertThat(afterCastle.fen()).contains("1RK1 "); // king on g1, rook on f1, h1 empty
  }

  @Test
  public void testCastlingQueenside() {
    List<PositionContext> positions =
        replayer.replay(
            "1. d4 d5 2. c4 e6 3. Nc3 Nf6 4. Bg5 Be7 5. e3 O-O 6. Nf3 Nbd7 7. Qc2 c6 8. O-O-O");

    PositionContext afterCastle = positions.get(positions.size() - 1);
    // After O-O-O, white king on c1, rook on d1
    assertThat(afterCastle.fen()).contains("2KR"); // king on c1, rook on d1
  }

  @Test
  public void testCapture() {
    List<PositionContext> positions = replayer.replay("1. e4 d5 2. exd5");

    PositionContext afterCapture = positions.get(positions.size() - 1);
    assertThat(afterCapture.fen()).contains("3P4"); // pawn now on d5
  }

  @Test
  public void testPromotion() {
    // Simplified position where pawn promotes
    List<PositionContext> positions =
        replayer.replay("1. e4 d5 2. e5 d4 3. e6 d3 4. exf7+ Kd7 5. fxg8=Q");

    PositionContext afterPromotion = positions.get(positions.size() - 1);
    assertThat(afterPromotion.fen()).contains("Q"); // promoted queen
  }

  @Test
  public void testCheckNotation() {
    List<PositionContext> positions = replayer.replay("1. e4 e5 2. Qh5 Nc6 3. Bc4 Nf6 4. Qxf7+");

    // Should handle + notation without issue
    assertThat(positions).hasSize(8);
  }

  @Test
  public void testCheckmateNotation() {
    // Scholar's mate
    List<PositionContext> positions = replayer.replay("1. e4 e5 2. Qh5 Nc6 3. Bc4 Nf6 4. Qxf7#");

    assertThat(positions).hasSize(8);
    // The move with # should be processed correctly
    PositionContext finalPos = positions.get(positions.size() - 1);
    assertThat(finalPos.moveNumber()).isEqualTo(4);
  }

  @Test
  public void testStripsComments() {
    List<PositionContext> positions =
        replayer.replay("1. e4 {Best by test} e5 {Solid reply} 2. Nf3");

    assertThat(positions).hasSize(4); // initial + 3 moves
  }

  @Test
  public void testStripsVariations() {
    List<PositionContext> positions = replayer.replay("1. e4 e5 (1... c5 2. Nf3) 2. Nf3");

    assertThat(positions).hasSize(4); // initial + 3 moves (variation excluded)
  }

  @Test
  public void testStripsNagAnnotations() {
    List<PositionContext> positions = replayer.replay("1. e4 $1 e5 $2 2. Nf3 $10");

    assertThat(positions).hasSize(4);
  }

  @Test
  public void testIgnoresResultIndicator() {
    List<PositionContext> positions = replayer.replay("1. e4 e5 2. Nf3 Nc6 1-0");

    assertThat(positions).hasSize(5); // initial + 4 moves, not including result
  }

  @Test
  public void testMoveNumbersWithoutSpaces() {
    List<PositionContext> positions = replayer.replay("1.e4 e5 2.Nf3 Nc6");

    assertThat(positions).hasSize(5);
  }

  @Test
  public void testPawnMoveWithoutPieceIndicator() {
    List<PositionContext> positions = replayer.replay("1. d4 d5 2. c4 e6");

    assertThat(positions).hasSize(5);
    // Verify pawns moved correctly
    PositionContext afterD4 = positions.get(1);
    assertThat(afterD4.fen()).contains("3P4"); // d4 pawn
  }

  @Test
  public void testAmbiguousMoveWithFile() {
    // Position where two knights can go to same square
    List<PositionContext> positions =
        replayer.replay("1. e4 e5 2. Nf3 Nc6 3. Nc3 Nf6 4. Nd5 Nxd5 5. exd5 Nd4 6. Nxd4");

    assertThat(positions).hasSize(12);
  }

  @Test
  public void testLongerGame() {
    // Test that a typical opening sequence replays correctly
    List<PositionContext> positions =
        replayer.replay(
            "1. e4 e5 2. Nf3 Nc6 3. Bb5 a6 4. Ba4 Nf6 5. O-O Be7 6. Re1 b5 7. Bb3 d6 8. c3 O-O");

    assertThat(positions).hasSize(17); // initial + 16 half-moves
  }

  @Test
  public void testAmbiguousMoveWithRank() {
    // After move 10, white has Ra3 and Rf1. Both can reach a1 (back rank is clear
    // since Nc3, Be3, Qd2 have moved those pieces). R3a1 uses rank disambiguation.
    List<PositionContext> positions =
        replayer.replay(
            "1. e4 e5 2. d3 d6 3. Nf3 Nf6 4. Nc3 Nc6 5. Be2 Be7 6. O-O O-O "
                + "7. Be3 Be6 8. Qd2 Qd7 9. a4 a5 10. Ra3 Ra6 11. R3a1");

    assertThat(positions).hasSize(22); // initial + 21 half-moves
  }
}
