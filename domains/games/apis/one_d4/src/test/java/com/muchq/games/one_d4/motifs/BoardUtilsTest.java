package com.muchq.games.one_d4.motifs;

import org.junit.Test;

import static org.assertj.core.api.Assertions.assertThat;

public class BoardUtilsTest {

    @Test
    public void parsePlacement_startingPosition() {
        String placement = "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR";
        int[][] board = BoardUtils.parsePlacement(placement);

        assertThat(board[0][0]).isEqualTo(-4); // black rook a8
        assertThat(board[0][4]).isEqualTo(-6); // black king e8
        assertThat(board[7][4]).isEqualTo(6); // white king e1
        assertThat(board[7][3]).isEqualTo(5); // white queen d1
        assertThat(board[4][4]).isEqualTo(0); // empty e4
    }

    @Test
    public void pieceValue_allPieces() {
        assertThat(BoardUtils.pieceValue('K')).isEqualTo(6);
        assertThat(BoardUtils.pieceValue('Q')).isEqualTo(5);
        assertThat(BoardUtils.pieceValue('R')).isEqualTo(4);
        assertThat(BoardUtils.pieceValue('B')).isEqualTo(3);
        assertThat(BoardUtils.pieceValue('N')).isEqualTo(2);
        assertThat(BoardUtils.pieceValue('P')).isEqualTo(1);
        assertThat(BoardUtils.pieceValue('k')).isEqualTo(-6);
        assertThat(BoardUtils.pieceValue('q')).isEqualTo(-5);
        assertThat(BoardUtils.pieceValue('r')).isEqualTo(-4);
        assertThat(BoardUtils.pieceValue('b')).isEqualTo(-3);
        assertThat(BoardUtils.pieceValue('n')).isEqualTo(-2);
        assertThat(BoardUtils.pieceValue('p')).isEqualTo(-1);
        assertThat(BoardUtils.pieceValue('x')).isEqualTo(0);
    }

}
