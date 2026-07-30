package com.muchq.games.one_d4.engine;

import static org.assertj.core.api.Assertions.assertThat;
import static org.assertj.core.api.Assertions.assertThatThrownBy;

import java.util.List;
import org.junit.jupiter.api.Test;

public class ReplayBoardTest {

  private static String placement(ReplayBoard board) {
    return board.toFEN().split(" ")[0];
  }

  private static ReplayBoard afterMoves(String... sans) {
    ReplayBoard board = ReplayBoard.standard();
    for (String san : sans) {
      board.play(san);
    }
    return board;
  }

  @Test
  public void startingPositionFen() {
    assertThat(ReplayBoard.standard().toFEN())
        .isEqualTo("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1");
  }

  @Test
  public void simplePushAndDoublePushSetEnPassantSquare() {
    ReplayBoard board = afterMoves("e4");
    assertThat(board.toFEN())
        .isEqualTo("rnbqkbnr/pppppppp/8/8/4P3/8/PPPP1PPP/RNBQKBNR b KQkq e3 0 1");

    board.play("e6"); // single push clears the ep square
    assertThat(board.toFEN().split(" ")[3]).isEqualTo("-");
  }

  @Test
  public void enPassantCaptureRemovesTheBypassedPawn() {
    ReplayBoard board = afterMoves("e4", "Nf6", "e5", "d5", "exd6");
    // d6 now holds the white pawn; both d5 (captured en passant) and e5 are empty
    assertThat(placement(board)).isEqualTo("rnbqkb1r/ppp1pppp/3P1n2/8/8/8/PPPP1PPP/RNBQKBNR");
  }

  @Test
  public void kingsideAndQueensideCastling() {
    ReplayBoard board = afterMoves("e4", "e5", "Nf3", "Nc6", "Bc4", "Bc5", "O-O");
    assertThat(placement(board)).endsWith("RNBQ1RK1");
    assertThat(board.toFEN().split(" ")[2]).isEqualTo("kq");

    ReplayBoard queenside =
        afterMoves("d4", "d5", "Nc3", "Nc6", "Bf4", "Bf5", "Qd2", "Qd7", "O-O-O", "O-O-O");
    assertThat(placement(queenside)).startsWith("2kr1bnr");
    assertThat(placement(queenside)).endsWith("2KR1BNR");
    assertThat(queenside.toFEN().split(" ")[2]).isEqualTo("-");
  }

  @Test
  public void promotionAndUnderpromotionWithCapture() {
    ReplayBoard board = afterMoves("g4", "h5", "gxh5", "g6", "hxg6", "Bh6", "g7", "Be3", "gxh8=N");
    // The g7 pawn captured the h8 rook and underpromoted to a white knight
    assertThat(placement(board).split("/")[0]).isEqualTo("rnbqk1nN");
    // capturing the h8 rook removes black's kingside castling right
    assertThat(board.toFEN().split(" ")[2]).isEqualTo("KQq");
  }

  @Test
  public void fileAndRankDisambiguation() {
    ReplayBoard knights =
        afterMoves("Nf3", "Nc6", "e3", "e5", "d4", "exd4", "exd4", "d5", "Bd3", "Bd6", "Nbd2");
    // b1 knight went to d2; f3 knight stayed put
    assertThat(placement(knights).split("/")[7]).isEqualTo("R1BQK2R");

    ReplayBoard rooks = ReplayBoard.fromFen("k7/8/8/4R3/8/8/8/4R2K w - - 0 1");
    rooks.play("R1e2");
    assertThat(placement(rooks)).isEqualTo("k7/8/8/4R3/8/8/4R3/7K");
  }

  @Test
  public void pinnedPieceResolvesImplicitDisambiguation() {
    // Both knights attack d5, but the e3 knight is pinned to the king by the e8 rook,
    // so SAN "Nd5" (no hint) must move the b4 knight.
    ReplayBoard board = ReplayBoard.fromFen("k3r3/8/8/8/1N6/4N3/8/4K3 w - - 0 1");
    board.play("Nd5");
    assertThat(placement(board)).isEqualTo("k3r3/8/8/3N4/8/4N3/8/4K3");
  }

  @Test
  public void blackPinnedPieceResolvesImplicitDisambiguation() {
    // Mirror of the white pin test: the e3 knight is pinned to the e1 king by the e8 rook.
    ReplayBoard board = ReplayBoard.fromFen("K3R3/8/8/8/1n6/4n3/8/4k3 b - - 0 1");
    board.play("Nd5");
    assertThat(placement(board)).isEqualTo("K3R3/8/8/3n4/8/4n3/8/4k3");
  }

  @Test
  public void blackRankDisambiguation() {
    ReplayBoard rooks = ReplayBoard.fromFen("K7/8/8/4r3/8/8/8/4r2k b - - 0 1");
    rooks.play("R1e2");
    assertThat(placement(rooks)).isEqualTo("K7/8/8/4r3/8/8/4r3/7k");
  }

  @Test
  public void ambiguousMoveWithoutHintThrows() {
    // Both knights can legally reach d5 and no hint is given — malformed SAN, not first-wins.
    ReplayBoard board = ReplayBoard.fromFen("k7/8/8/8/1N3N2/8/8/K7 w - - 0 1");
    assertThatThrownBy(() -> board.play("Nd5"))
        .isInstanceOf(IllegalArgumentException.class)
        .hasMessageContaining("Ambiguous");
  }

  @Test
  public void enPassantSquareClearedByPieceMoveAndCastling() {
    // A piece move after a double push must clear the ep field; chariot rarely prints ep, so
    // the parity test's lenient comparison would not catch a stale square here.
    ReplayBoard board = afterMoves("e4", "Nf6");
    assertThat(board.toFEN().split(" ")[3]).isEqualTo("-");

    ReplayBoard castled = afterMoves("e4", "e5", "Nf3", "Nf6", "Bc4", "d5", "O-O");
    assertThat(castled.toFEN().split(" ")[3]).isEqualTo("-");
  }

  @Test
  public void whiteQueensideRookMoveClearsCastlingRight() {
    ReplayBoard board = afterMoves("a4", "h5", "Ra3", "Rh6");
    assertThat(board.toFEN().split(" ")[2]).isEqualTo("Kq");
  }

  @Test
  public void pawnCaptureToEmptySquareWithoutEnPassantThrows() {
    // Not en passant: d5 is empty and no pawn just double-pushed past — must throw, not
    // phantom-capture.
    ReplayBoard board = afterMoves("e4", "Nf6");
    assertThatThrownBy(() -> board.play("exd5")).isInstanceOf(IllegalArgumentException.class);

    // The en-passant removal must only ever remove an enemy pawn, never another piece.
    ReplayBoard knightOnEpSquare = ReplayBoard.fromFen("k7/8/8/3nP3/8/8/8/K7 w - - 0 1");
    assertThatThrownBy(() -> knightOnEpSquare.play("exd6"))
        .isInstanceOf(IllegalArgumentException.class);
    assertThat(placement(knightOnEpSquare)).isEqualTo("k7/8/8/3nP3/8/8/8/K7");
  }

  @Test
  public void doublePushFromNonHomeRankThrows() {
    ReplayBoard board = ReplayBoard.fromFen("k7/8/8/8/8/4P3/8/K7 w - - 0 1");
    assertThatThrownBy(() -> board.play("e5")).isInstanceOf(IllegalArgumentException.class);
  }

  @Test
  public void blockedDoublePushThrows() {
    ReplayBoard board = ReplayBoard.fromFen("k7/8/8/8/8/4N3/4P3/K7 w - - 0 1");
    assertThatThrownBy(() -> board.play("e4"))
        .isInstanceOf(IllegalArgumentException.class)
        .hasMessageContaining("Blocked");
  }

  @Test
  public void castlingWithPiecesOutOfPlaceThrows() {
    // Path blocked on the untouched starting board
    ReplayBoard blocked = ReplayBoard.standard();
    assertThatThrownBy(() -> blocked.play("O-O")).isInstanceOf(IllegalArgumentException.class);

    // Rook missing from its corner
    ReplayBoard noRook = ReplayBoard.fromFen("k7/8/8/8/8/8/8/4K3 w - - 0 1");
    assertThatThrownBy(() -> noRook.play("O-O-O")).isInstanceOf(IllegalArgumentException.class);
  }

  @Test
  public void doubleDisambiguationResolvesAmongThreeQueens() {
    // Queens on a1, a5, and e1 all attack e5: the file hint alone matches a1/a5 and the rank
    // hint alone matches a1/e1, so SAN needs both — Qa1e5.
    ReplayBoard board = ReplayBoard.fromFen("k7/8/8/Q7/8/8/8/Q3Q2K w - - 0 1");
    board.play("Qa1e5");
    assertThat(placement(board)).isEqualTo("k7/8/8/Q3Q3/8/8/8/4Q2K");
  }

  @Test
  public void promotionWithoutEqualsSignAccepted() {
    // chess.com SAN normally writes e8=Q, but the bare e8Q form appears in the wild.
    ReplayBoard board = ReplayBoard.fromFen("k7/4P3/8/8/8/8/8/K7 w - - 0 1");
    board.play("e8Q");
    assertThat(placement(board).split("/")[0]).isEqualTo("k3Q3");
  }

  @Test
  public void rookMovesAndCapturesUpdateCastlingRights() {
    ReplayBoard board = afterMoves("h4", "a5", "Rh3", "Ra6");
    assertThat(board.toFEN().split(" ")[2]).isEqualTo("Qk");
  }

  @Test
  public void halfmoveAndFullmoveCounters() {
    ReplayBoard board = afterMoves("Nf3", "Nf6", "Ng1", "Ng8");
    String[] fields = board.toFEN().split(" ");
    assertThat(fields[4]).isEqualTo("4"); // four non-pawn, non-capture plies
    assertThat(fields[5]).isEqualTo("3");

    board.play("e4"); // pawn move resets the clock
    assertThat(board.toFEN().split(" ")[4]).isEqualTo("0");
  }

  @Test
  public void checkAndMateSuffixesAreIgnored() {
    ReplayBoard board = afterMoves("e4", "e5", "Qh5", "Nc6", "Bc4", "Nf6", "Qxf7#");
    // Scholar's mate: the white queen sits on f7
    assertThat(placement(board).split("/")[1]).isEqualTo("pppp1Qpp");
  }

  @Test
  public void malformedSanThrows() {
    for (String bad : List.of("Zf3", "e9", "hello")) {
      ReplayBoard board = ReplayBoard.standard();
      assertThatThrownBy(() -> board.play(bad)).isInstanceOf(IllegalArgumentException.class);
    }
    // Legal-looking but impossible: no pawn can push there
    ReplayBoard board = ReplayBoard.standard();
    board.play("e4");
    assertThatThrownBy(() -> board.play("e4")).isInstanceOf(IllegalArgumentException.class);
  }
}
