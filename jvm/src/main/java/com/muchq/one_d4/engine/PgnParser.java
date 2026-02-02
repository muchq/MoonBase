package com.muchq.one_d4.engine;

import com.muchq.one_d4.engine.model.ParsedGame;

import java.util.LinkedHashMap;
import java.util.Map;
import java.util.regex.Matcher;
import java.util.regex.Pattern;

public class PgnParser {
    private static final Pattern HEADER_PATTERN = Pattern.compile("\\[\\s*(\\w+)\\s+\"([^\"]*)\"\\s*]");

    public ParsedGame parse(String pgn) {
        Map<String, String> headers = new LinkedHashMap<>();
        StringBuilder moveText = new StringBuilder();
        boolean inMoves = false;

        for (String line : pgn.split("\\r?\\n")) {
            String trimmed = line.trim();
            if (trimmed.isEmpty()) {
                if (!headers.isEmpty()) {
                    inMoves = true;
                }
                continue;
            }

            if (!inMoves) {
                Matcher m = HEADER_PATTERN.matcher(trimmed);
                if (m.matches()) {
                    headers.put(m.group(1), m.group(2));
                    continue;
                }
            }

            // If we've seen headers and this line doesn't match a header, it's movetext
            inMoves = true;
            if (!moveText.isEmpty()) {
                moveText.append(' ');
            }
            moveText.append(trimmed);
        }

        return new ParsedGame(headers, moveText.toString());
    }
}
