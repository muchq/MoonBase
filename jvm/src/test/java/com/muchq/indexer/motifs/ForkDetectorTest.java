package com.muchq.indexer.motifs;

import com.muchq.indexer.engine.model.GameFeatures;
import com.muchq.indexer.engine.model.Motif;
import com.muchq.indexer.engine.model.PositionContext;
import org.junit.Test;

import java.util.List;

import static org.assertj.core.api.Assertions.assertThat;

public class ForkDetectorTest {

    private final ForkDetector detector = new ForkDetector();

    @Test
    public void motifType() {
        assertThat(detector.motif()).isEqualTo(Motif.FORK);
    }

    // === Knight forks ===

    @Test
    public void knightFork_attacksKingAndQueen() {
        // White knight on e6 forks black king on g7 and black queen on c7
        // Position: 8/2q3k1/4N3/8/8/8/8/4K3 w - - 0 1
        String fen = "8/2q3k1/4N3/8/8/8/8/4K3 w - - 0 1";
        List<PositionContext> positions = List.of(
                new PositionContext(10, fen, true)
        );

        // After white's move, it's black to move, so attacker is white (!whiteToMove)
        // But ForkDetector checks for the side that just moved, which is white here
        // Let's set whiteToMove=false to indicate black is to move (white just moved)
        positions = List.of(new PositionContext(10, fen, false));

        List<GameFeatures.MotifOccurrence> occurrences = detector.detect(positions);
        assertThat(occurrences).hasSize(1);
        assertThat(occurrences.get(0).moveNumber()).isEqualTo(10);
    }

    @Test
    public void knightFork_attacksKingAndRook() {
        // White knight on d5 forks black king on f6 and black rook on b4
        // Position: 8/8/5k2/3N4/1r6/8/8/4K3 b - - 0 1
        String fen = "8/8/5k2/3N4/1r6/8/8/4K3 b - - 0 1";
        List<PositionContext> positions = List.of(
                new PositionContext(15, fen, false)  // black to move, white just forked
        );

        List<GameFeatures.MotifOccurrence> occurrences = detector.detect(positions);
        assertThat(occurrences).hasSize(1);
    }

    @Test
    public void knightFork_attacksQueenAndRook() {
        // White knight on c6 forks black queen on e7 and black rook on a7
        String fen = "8/r3q3/2N5/8/8/8/8/4K2k b - - 0 1";
        List<PositionContext> positions = List.of(
                new PositionContext(12, fen, false)
        );

        List<GameFeatures.MotifOccurrence> occurrences = detector.detect(positions);
        assertThat(occurrences).hasSize(1);
    }

    @Test
    public void knightFork_blackKnightForksWhitePieces() {
        // Black knight on d4 forks white king on e2 and white queen on f5
        String fen = "8/8/8/5Q2/3n4/8/4K3/7k w - - 0 1";
        List<PositionContext> positions = List.of(
                new PositionContext(20, fen, true)  // white to move, black just forked
        );

        List<GameFeatures.MotifOccurrence> occurrences = detector.detect(positions);
        assertThat(occurrences).hasSize(1);
    }

    // === Pawn forks ===

    @Test
    public void pawnFork_whitePawnForksBlackPieces() {
        // White pawn on d5 attacks black knights on c6 and e6
        String fen = "8/8/2n1n3/3P4/8/8/8/4K2k b - - 0 1";
        List<PositionContext> positions = List.of(
                new PositionContext(8, fen, false)
        );

        List<GameFeatures.MotifOccurrence> occurrences = detector.detect(positions);
        assertThat(occurrences).hasSize(1);
    }

    @Test
    public void pawnFork_blackPawnForksWhitePieces() {
        // Black pawn on e4 attacks white bishop on d3 and white knight on f3
        String fen = "7k/8/8/8/4p3/3B1N2/8/4K3 w - - 0 1";
        List<PositionContext> positions = List.of(
                new PositionContext(15, fen, true)
        );

        List<GameFeatures.MotifOccurrence> occurrences = detector.detect(positions);
        assertThat(occurrences).hasSize(1);
    }

    // === Queen forks ===

    @Test
    public void queenFork_attacksKingAndRook() {
        // White queen on e5 forks black king on g7 and black rook on a5
        String fen = "8/6k1/8/r3Q3/8/8/8/4K3 b - - 0 1";
        List<PositionContext> positions = List.of(
                new PositionContext(25, fen, false)
        );

        List<GameFeatures.MotifOccurrence> occurrences = detector.detect(positions);
        assertThat(occurrences).hasSize(1);
    }

    // === Bishop forks ===

    @Test
    public void bishopFork_attacksTwoRooks() {
        // White bishop on c4 attacks black rooks on a6 and f7
        String fen = "8/5r2/r7/8/2B5/8/8/4K2k b - - 0 1";
        List<PositionContext> positions = List.of(
                new PositionContext(18, fen, false)
        );

        List<GameFeatures.MotifOccurrence> occurrences = detector.detect(positions);
        assertThat(occurrences).hasSize(1);
    }

    // === No fork cases ===

    @Test
    public void noFork_onlyOnePieceAttacked() {
        // White knight on e4 attacks only black queen on f6
        String fen = "8/8/5q2/8/4N3/8/8/4K2k b - - 0 1";
        List<PositionContext> positions = List.of(
                new PositionContext(10, fen, false)
        );

        List<GameFeatures.MotifOccurrence> occurrences = detector.detect(positions);
        assertThat(occurrences).isEmpty();
    }

    @Test
    public void noFork_attackingPawnsNotCounted() {
        // White knight attacks two black pawns - pawns are value 1, not counted as valuable
        String fen = "8/8/8/8/3N4/2p1p3/8/4K2k b - - 0 1";
        List<PositionContext> positions = List.of(
                new PositionContext(10, fen, false)
        );

        List<GameFeatures.MotifOccurrence> occurrences = detector.detect(positions);
        assertThat(occurrences).isEmpty();
    }

    @Test
    public void noFork_emptyPosition() {
        // Starting position - no forks
        String fen = "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1";
        List<PositionContext> positions = List.of(
                new PositionContext(1, fen, true)
        );

        List<GameFeatures.MotifOccurrence> occurrences = detector.detect(positions);
        assertThat(occurrences).isEmpty();
    }

    @Test
    public void noFork_emptyList() {
        List<GameFeatures.MotifOccurrence> occurrences = detector.detect(List.of());
        assertThat(occurrences).isEmpty();
    }

    // === Multiple positions ===

    @Test
    public void multiplePositions_detectsForksInSome() {
        List<PositionContext> positions = List.of(
                // No fork
                new PositionContext(1, "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1", true),
                // Fork: knight on e6 forks king g7 and queen c7
                new PositionContext(10, "8/2q3k1/4N3/8/8/8/8/4K3 b - - 0 1", false),
                // No fork
                new PositionContext(15, "8/8/8/8/8/8/8/4K2k w - - 0 1", true)
        );

        List<GameFeatures.MotifOccurrence> occurrences = detector.detect(positions);
        assertThat(occurrences).hasSize(1);
        assertThat(occurrences.get(0).moveNumber()).isEqualTo(10);
    }
}
