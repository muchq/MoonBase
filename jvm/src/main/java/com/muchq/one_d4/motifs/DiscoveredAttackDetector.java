package com.muchq.one_d4.motifs;

import com.muchq.one_d4.engine.model.GameFeatures;
import com.muchq.one_d4.engine.model.Motif;
import com.muchq.one_d4.engine.model.PositionContext;

import java.util.ArrayList;
import java.util.List;

public class DiscoveredAttackDetector implements MotifDetector {

    @Override
    public Motif motif() {
        return Motif.DISCOVERED_ATTACK;
    }

    @Override
    public List<GameFeatures.MotifOccurrence> detect(List<PositionContext> positions) {
        List<GameFeatures.MotifOccurrence> occurrences = new ArrayList<>();

        // Compare consecutive positions to detect discovered attacks.
        // A discovered attack occurs when a piece moves and reveals an attack
        // from a sliding piece behind it.
        for (int i = 1; i < positions.size(); i++) {
            PositionContext before = positions.get(i - 1);
            PositionContext after = positions.get(i);

            String beforePlacement = before.fen().split(" ")[0];
            String afterPlacement = after.fen().split(" ")[0];
            int[][] boardBefore = PinDetector.parsePlacement(beforePlacement);
            int[][] boardAfter = PinDetector.parsePlacement(afterPlacement);

            // The side that just moved is the opposite of whose turn it now is
            boolean moverIsWhite = after.whiteToMove() ? false : true;

            if (hasDiscoveredAttack(boardBefore, boardAfter, moverIsWhite)) {
                occurrences.add(new GameFeatures.MotifOccurrence(
                        after.moveNumber(), "Discovered attack at move " + after.moveNumber()));
            }
        }

        return occurrences;
    }

    private boolean hasDiscoveredAttack(int[][] before, int[][] after, boolean moverIsWhite) {
        // Find the piece that moved (square that became empty)
        for (int r = 0; r < 8; r++) {
            for (int c = 0; c < 8; c++) {
                int pieceBefore = before[r][c];
                int pieceAfter = after[r][c];

                if (pieceBefore != 0 && pieceAfter == 0) {
                    boolean isWhite = pieceBefore > 0;
                    if (isWhite == moverIsWhite) {
                        // This square was vacated by the moving piece.
                        // Check if any sliding piece behind it now has a new attack line.
                        if (revealsAttack(after, r, c, moverIsWhite)) {
                            return true;
                        }
                    }
                }
            }
        }
        return false;
    }

    private boolean revealsAttack(int[][] board, int vacatedR, int vacatedC, boolean moverIsWhite) {
        int[][] directions = {{0,1},{0,-1},{1,0},{-1,0},{1,1},{1,-1},{-1,1},{-1,-1}};

        for (int[] dir : directions) {
            // Look behind the vacated square for a friendly sliding piece
            int br = vacatedR - dir[0], bc = vacatedC - dir[1];
            while (br >= 0 && br < 8 && bc >= 0 && bc < 8) {
                int piece = board[br][bc];
                if (piece != 0) {
                    boolean isWhite = piece > 0;
                    if (isWhite == moverIsWhite && isSlidingAttacker(piece, dir)) {
                        // Check if there's an enemy piece along the forward direction
                        int fr = vacatedR + dir[0], fc = vacatedC + dir[1];
                        while (fr >= 0 && fr < 8 && fc >= 0 && fc < 8) {
                            int target = board[fr][fc];
                            if (target != 0) {
                                boolean targetIsWhite = target > 0;
                                if (targetIsWhite != moverIsWhite && Math.abs(target) >= 2) {
                                    return true;
                                }
                                break;
                            }
                            fr += dir[0];
                            fc += dir[1];
                        }
                    }
                    break;
                }
                br -= dir[0];
                bc -= dir[1];
            }
        }
        return false;
    }

    private boolean isSlidingAttacker(int piece, int[] dir) {
        int absPiece = Math.abs(piece);
        boolean isDiagonal = dir[0] != 0 && dir[1] != 0;
        boolean isStraight = dir[0] == 0 || dir[1] == 0;
        if (absPiece == 5) return true;
        if (absPiece == 3 && isDiagonal) return true;
        if (absPiece == 4 && isStraight) return true;
        return false;
    }
}
