package com.muchq.one_d4.worker;

/**
 * Maps chess.com API result values to standard chess notation.
 *
 * Chess.com returns separate result strings for white and black:
 * - "win" for the winning side
 * - "resigned", "checkmated", "timeout", "abandoned" for the losing side
 * - "agreed", "repetition", "stalemate", "insufficient", "50move", "timevsinsufficient" for draws
 *
 * This class normalizes these to standard notation: "1-0", "0-1", "1/2-1/2", or "unknown".
 */
public class ResultMapper {

    /**
     * Determines the game result in standard notation.
     *
     * @param whiteResult chess.com result string for white (may be null)
     * @param blackResult chess.com result string for black (may be null)
     * @return "1-0" (white wins), "0-1" (black wins), "1/2-1/2" (draw), or "unknown"
     */
    public static String mapResult(String whiteResult, String blackResult) {
        if (whiteResult == null && blackResult == null) {
            return "unknown";
        }

        // Explicit win
        if ("win".equals(whiteResult)) {
            return "1-0";
        }
        if ("win".equals(blackResult)) {
            return "0-1";
        }

        // Draw results
        if (isDrawResult(whiteResult) || isDrawResult(blackResult)) {
            return "1/2-1/2";
        }

        // White lost → black won
        if (isLossResult(whiteResult)) {
            return "0-1";
        }
        // Black lost → white won
        if (isLossResult(blackResult)) {
            return "1-0";
        }

        return "unknown";
    }

    /**
     * Checks if the result string indicates a draw.
     */
    public static boolean isDrawResult(String result) {
        if (result == null) return false;
        return switch (result) {
            case "agreed", "repetition", "stalemate", "insufficient",
                 "50move", "timevsinsufficient", "drawn" -> true;
            default -> false;
        };
    }

    /**
     * Checks if the result string indicates a loss for that player.
     */
    public static boolean isLossResult(String result) {
        if (result == null) return false;
        return switch (result) {
            case "resigned", "checkmated", "timeout", "abandoned", "lose" -> true;
            default -> false;
        };
    }
}
