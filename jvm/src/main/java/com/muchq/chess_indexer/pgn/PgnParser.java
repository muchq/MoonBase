package com.muchq.chess_indexer.pgn;

import java.util.ArrayList;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import java.util.regex.Matcher;
import java.util.regex.Pattern;

public class PgnParser {

  private static final Pattern TAG_PATTERN = Pattern.compile("\\[(\\w+)\\s+\"([^\"]*)\"\\]");

  public PgnGame parse(String pgn) {
    Map<String, String> tags = new HashMap<>();
    StringBuilder movetext = new StringBuilder();

    for (String line : pgn.split("\\R")) {
      String trimmed = line.trim();
      if (trimmed.startsWith("[")) {
        Matcher matcher = TAG_PATTERN.matcher(trimmed);
        if (matcher.find()) {
          tags.put(matcher.group(1), matcher.group(2));
        }
      } else if (!trimmed.isEmpty()) {
        movetext.append(trimmed).append(' ');
      }
    }

    String cleaned = stripComments(movetext.toString());
    cleaned = stripVariations(cleaned);
    List<String> moves = tokenizeMoves(cleaned);

    return new PgnGame(tags, moves);
  }

  private String stripComments(String movetext) {
    StringBuilder out = new StringBuilder();
    boolean inBrace = false;
    boolean inLineComment = false;
    for (int i = 0; i < movetext.length(); i++) {
      char c = movetext.charAt(i);
      if (inLineComment) {
        if (c == '\n') {
          inLineComment = false;
        }
        continue;
      }
      if (inBrace) {
        if (c == '}') {
          inBrace = false;
        }
        continue;
      }
      if (c == '{') {
        inBrace = true;
        continue;
      }
      if (c == ';') {
        inLineComment = true;
        continue;
      }
      out.append(c);
    }
    return out.toString();
  }

  private String stripVariations(String movetext) {
    StringBuilder out = new StringBuilder();
    int depth = 0;
    for (int i = 0; i < movetext.length(); i++) {
      char c = movetext.charAt(i);
      if (c == '(') {
        depth++;
        continue;
      }
      if (c == ')') {
        if (depth > 0) {
          depth--;
        }
        continue;
      }
      if (depth == 0) {
        out.append(c);
      }
    }
    return out.toString();
  }

  private List<String> tokenizeMoves(String movetext) {
    List<String> moves = new ArrayList<>();
    for (String raw : movetext.trim().split("\\s+")) {
      if (raw.isEmpty()) {
        continue;
      }
      if (isMoveNumber(raw) || isResult(raw) || isNag(raw)) {
        continue;
      }
      moves.add(stripAnnotations(raw));
    }
    return moves;
  }

  private boolean isMoveNumber(String token) {
    return token.matches("\\d+\\.{1,3}") || token.matches("\\d+\\.");
  }

  private boolean isResult(String token) {
    return token.equals("1-0") || token.equals("0-1") || token.equals("1/2-1/2") || token.equals("*");
  }

  private boolean isNag(String token) {
    return token.startsWith("$");
  }

  private String stripAnnotations(String token) {
    return token.replaceAll("[!?]+$", "");
  }
}
