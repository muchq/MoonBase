package com.muchq.one_d4.motifs;

import com.muchq.one_d4.engine.model.GameFeatures;
import com.muchq.one_d4.engine.model.Motif;
import com.muchq.one_d4.engine.model.PositionContext;

import java.util.ArrayList;
import java.util.List;

public class CrossPinDetector implements MotifDetector {

    @Override
    public Motif motif() {
        return Motif.CROSS_PIN;
    }

    @Override
    public List<GameFeatures.MotifOccurrence> detect(List<PositionContext> positions) {
        List<GameFeatures.MotifOccurrence> occurrences = new ArrayList<>();

        for (PositionContext ctx : positions) {
            String placement = ctx.fen().split(" ")[0];
            int[][] board = PinDetector.parsePlacement(placement);

            // A cross-pin occurs when a piece is pinned along two different directions
            // simultaneously (e.g., pinned by a rook on a file AND a bishop on a diagonal).
            if (hasCrossPin(board, ctx.whiteToMove())) {
                occurrences.add(new GameFeatures.MotifOccurrence(
                        ctx.moveNumber(), "Cross-pin detected at move " + ctx.moveNumber()));
            }
        }

        return occurrences;
    }

    private boolean hasCrossPin(int[][] board, boolean whiteToMove) {
        int kingPiece = whiteToMove ? 6 : -6;
        int kingRow = -1, kingCol = -1;

        for (int r = 0; r < 8; r++) {
            for (int c = 0; c < 8; c++) {
                if (board[r][c] == kingPiece) {
                    kingRow = r;
                    kingCol = c;
                }
            }
        }
        if (kingRow == -1) return false;

        // Find all pinned pieces and check if any piece is pinned along two axes
        int[][] directions = {{0,1},{0,-1},{1,0},{-1,0},{1,1},{1,-1},{-1,1},{-1,-1}};
        List<int[]> pinnedSquares = new ArrayList<>();

        for (int[] dir : directions) {
            int[] pinned = findPinnedPiece(board, kingRow, kingCol, dir[0], dir[1], whiteToMove);
            if (pinned != null) {
                pinnedSquares.add(pinned);
            }
        }

        // Check for duplicate pinned squares (same piece pinned from two directions)
        for (int i = 0; i < pinnedSquares.size(); i++) {
            for (int j = i + 1; j < pinnedSquares.size(); j++) {
                if (pinnedSquares.get(i)[0] == pinnedSquares.get(j)[0]
                        && pinnedSquares.get(i)[1] == pinnedSquares.get(j)[1]) {
                    return true;
                }
            }
        }
        return false;
    }

    private int[] findPinnedPiece(int[][] board, int kr, int kc, int dr, int dc, boolean whiteKing) {
        int r = kr + dr, c = kc + dc;
        int[] friendlyPos = null;

        while (r >= 0 && r < 8 && c >= 0 && c < 8) {
            int piece = board[r][c];
            if (piece != 0) {
                boolean isWhitePiece = piece > 0;
                if (isWhitePiece == whiteKing) {
                    if (friendlyPos != null) return null;
                    friendlyPos = new int[]{r, c};
                } else {
                    if (friendlyPos != null && isSlidingAttacker(piece, dr, dc)) {
                        return friendlyPos;
                    }
                    return null;
                }
            }
            r += dr;
            c += dc;
        }
        return null;
    }

    private boolean isSlidingAttacker(int piece, int dr, int dc) {
        int absPiece = Math.abs(piece);
        boolean isDiagonal = dr != 0 && dc != 0;
        boolean isStraight = dr == 0 || dc == 0;
        if (absPiece == 5) return true;
        if (absPiece == 3 && isDiagonal) return true;
        if (absPiece == 4 && isStraight) return true;
        return false;
    }
}
