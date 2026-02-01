package com.muchq.indexer.engine;

import chariot.util.Board;
import com.muchq.indexer.engine.model.PositionContext;

import java.util.ArrayList;
import java.util.List;
import java.util.regex.Matcher;
import java.util.regex.Pattern;

public class GameReplayer {
    private static final Pattern MOVE_PATTERN = Pattern.compile(
            "(?:\\d+\\.\\s*)?([KQRBNP]?[a-h]?[1-8]?x?[a-h][1-8](?:=[QRBN])?[+#]?|O-O-O|O-O)"
    );

    public List<PositionContext> replay(String moveText) {
        List<PositionContext> positions = new ArrayList<>();
        Board board = Board.fromStandardPosition();

        positions.add(new PositionContext(0, board.toFEN(), true));

        List<String> moves = extractMoves(moveText);
        int moveNumber = 1;
        boolean whiteToMove = true;

        for (String move : moves) {
            board = board.play(move);
            if (!whiteToMove) {
                moveNumber++;
            }
            whiteToMove = !whiteToMove;
            positions.add(new PositionContext(moveNumber, board.toFEN(), whiteToMove));
        }

        return positions;
    }

    private List<String> extractMoves(String moveText) {
        List<String> moves = new ArrayList<>();
        // Remove comments and variations
        String cleaned = moveText.replaceAll("\\{[^}]*}", "")
                .replaceAll("\\([^)]*\\)", "")
                .replaceAll("\\$\\d+", "");

        Matcher m = MOVE_PATTERN.matcher(cleaned);
        while (m.find()) {
            String move = m.group(1);
            // Skip result indicators
            if (!move.equals("1-0") && !move.equals("0-1") && !move.equals("1/2-1/2")) {
                moves.add(move);
            }
        }
        return moves;
    }
}
