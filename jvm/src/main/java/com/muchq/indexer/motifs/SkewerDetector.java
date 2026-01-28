package com.muchq.indexer.motifs;

import com.muchq.indexer.engine.model.GameFeatures;
import com.muchq.indexer.engine.model.Motif;
import com.muchq.indexer.engine.model.PositionContext;

import java.util.ArrayList;
import java.util.List;

public class SkewerDetector implements MotifDetector {

    @Override
    public Motif motif() {
        return Motif.SKEWER;
    }

    @Override
    public List<GameFeatures.MotifOccurrence> detect(List<PositionContext> positions) {
        List<GameFeatures.MotifOccurrence> occurrences = new ArrayList<>();

        for (PositionContext ctx : positions) {
            String placement = ctx.fen().split(" ")[0];
            int[][] board = PinDetector.parsePlacement(placement);

            // A skewer is the opposite of a pin: a more valuable piece is in front,
            // and when it moves, a less valuable piece behind is captured.
            if (hasSkewer(board, !ctx.whiteToMove())) {
                occurrences.add(new GameFeatures.MotifOccurrence(
                        ctx.moveNumber(), "Skewer detected at move " + ctx.moveNumber()));
            }
        }

        return occurrences;
    }

    private boolean hasSkewer(int[][] board, boolean attackerIsWhite) {
        int[][] directions = {{0,1},{0,-1},{1,0},{-1,0},{1,1},{1,-1},{-1,1},{-1,-1}};

        for (int r = 0; r < 8; r++) {
            for (int c = 0; c < 8; c++) {
                int piece = board[r][c];
                if (piece == 0) continue;
                boolean isWhite = piece > 0;
                if (isWhite != attackerIsWhite) continue;

                int absPiece = Math.abs(piece);
                // Only sliding pieces can skewer
                if (absPiece != 3 && absPiece != 4 && absPiece != 5) continue;

                for (int[] dir : directions) {
                    if (!canAttackDirection(absPiece, dir)) continue;
                    if (isSkewerAlongRay(board, r, c, dir[0], dir[1], attackerIsWhite)) {
                        return true;
                    }
                }
            }
        }
        return false;
    }

    private boolean canAttackDirection(int absPiece, int[] dir) {
        boolean isDiagonal = dir[0] != 0 && dir[1] != 0;
        boolean isStraight = dir[0] == 0 || dir[1] == 0;
        if (absPiece == 5) return true; // Queen
        if (absPiece == 3) return isDiagonal; // Bishop
        if (absPiece == 4) return isStraight; // Rook
        return false;
    }

    private boolean isSkewerAlongRay(int[][] board, int ar, int ac, int dr, int dc, boolean attackerIsWhite) {
        int r = ar + dr, c = ac + dc;
        int firstValue = -1;

        while (r >= 0 && r < 8 && c >= 0 && c < 8) {
            int piece = board[r][c];
            if (piece != 0) {
                boolean isWhite = piece > 0;
                if (isWhite == attackerIsWhite) return false; // friendly piece blocks

                int value = Math.abs(piece);
                if (firstValue == -1) {
                    firstValue = value;
                } else {
                    // Skewer: first piece (in front) is more valuable than second
                    return firstValue > value && value >= 2;
                }
            }
            r += dr;
            c += dc;
        }
        return false;
    }
}
