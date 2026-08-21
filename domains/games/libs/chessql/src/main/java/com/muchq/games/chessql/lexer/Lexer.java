package com.muchq.games.chessql.lexer;

import java.util.ArrayList;
import java.util.List;
import java.util.Map;

public class Lexer {
  private static final Map<String, TokenType> KEYWORDS =
      Map.ofEntries(
          Map.entry("AND", TokenType.AND),
          Map.entry("OR", TokenType.OR),
          Map.entry("NOT", TokenType.NOT),
          Map.entry("IN", TokenType.IN),
          Map.entry("motif", TokenType.MOTIF),
          Map.entry("ORDER", TokenType.ORDER),
          Map.entry("BY", TokenType.BY),
          Map.entry("ASC", TokenType.ASC),
          Map.entry("DESC", TokenType.DESC),
          Map.entry("motif_count", TokenType.MOTIF_COUNT),
          Map.entry("sequence", TokenType.SEQUENCE),
          Map.entry("THEN", TokenType.THEN));

  private final String input;
  private int pos;

  public Lexer(String input) {
    this.input = input;
    this.pos = 0;
  }

  public List<Token> tokenize() {
    List<Token> tokens = new ArrayList<>();
    while (pos < input.length()) {
      char c = input.charAt(pos);

      if (Character.isWhitespace(c)) {
        pos++;
        continue;
      }

      if (c == '(') {
        tokens.add(new Token(TokenType.LPAREN, "(", pos++));
      } else if (c == ')') {
        tokens.add(new Token(TokenType.RPAREN, ")", pos++));
      } else if (c == '[') {
        tokens.add(new Token(TokenType.LBRACKET, "[", pos++));
      } else if (c == ']') {
        tokens.add(new Token(TokenType.RBRACKET, "]", pos++));
      } else if (c == ',') {
        tokens.add(new Token(TokenType.COMMA, ",", pos++));
      } else if (c == '.') {
        tokens.add(new Token(TokenType.DOT, ".", pos++));
      } else if (c == '=') {
        tokens.add(new Token(TokenType.EQ, "=", pos++));
      } else if (c == '!' && peek() == '=') {
        tokens.add(new Token(TokenType.NEQ, "!=", pos));
        pos += 2;
      } else if (c == '<' && peek() == '=') {
        tokens.add(new Token(TokenType.LTE, "<=", pos));
        pos += 2;
      } else if (c == '<') {
        tokens.add(new Token(TokenType.LT, "<", pos++));
      } else if (c == '>' && peek() == '=') {
        tokens.add(new Token(TokenType.GTE, ">=", pos));
        pos += 2;
      } else if (c == '>') {
        tokens.add(new Token(TokenType.GT, ">", pos++));
      } else if (c == '"') {
        tokens.add(readString());
      } else if (Character.isDigit(c)
          || (c == '-' && pos + 1 < input.length() && Character.isDigit(input.charAt(pos + 1)))) {
        tokens.add(readNumber());
      } else if (Character.isLetter(c) || c == '_') {
        tokens.add(readIdentifierOrKeyword());
      } else if (c == '\'') {
        throw new IllegalArgumentException(singleQuoteMessage());
      } else {
        throw new IllegalArgumentException("Unexpected character '" + c + "' at position " + pos);
      }
    }

    tokens.add(new Token(TokenType.EOF, "", pos));
    return tokens;
  }

  private char peek() {
    return pos + 1 < input.length() ? input.charAt(pos + 1) : '\0';
  }

  /**
   * The SQL habit: 'B90' instead of "B90". The generic unexpected-character error is technically
   * true here and practically useless; the fix is one specific thing, so name it — echoing the
   * would-be string back double-quoted, but only when the input holds exactly two single quotes.
   * With any other count the closing quote is ambiguous ('King's Gambit' would echo back the
   * truncated "King"), and saying less beats suggesting wrong.
   */
  private String singleQuoteMessage() {
    int close = input.lastIndexOf('\'');
    String example =
        input.chars().filter(c -> c == '\'').count() == 2 && close > pos
            ? ": try \"" + input.substring(pos + 1, close) + "\""
            : "";
    return "Strings use double quotes, not single quotes" + example + " (at position " + pos + ")";
  }

  private Token readString() {
    int start = pos;
    pos++; // skip opening quote
    StringBuilder sb = new StringBuilder();
    while (pos < input.length() && input.charAt(pos) != '"') {
      if (input.charAt(pos) == '\\' && pos + 1 < input.length()) {
        pos++;
        sb.append(input.charAt(pos));
      } else {
        sb.append(input.charAt(pos));
      }
      pos++;
    }
    if (pos >= input.length()) {
      throw new IllegalArgumentException("Unterminated string at position " + start);
    }
    pos++; // skip closing quote
    return new Token(TokenType.STRING, sb.toString(), start);
  }

  private Token readNumber() {
    int start = pos;
    if (input.charAt(pos) == '-') {
      pos++;
    }
    while (pos < input.length() && Character.isDigit(input.charAt(pos))) {
      pos++;
    }
    return new Token(TokenType.NUMBER, input.substring(start, pos), start);
  }

  private Token readIdentifierOrKeyword() {
    int start = pos;
    while (pos < input.length()
        && (Character.isLetterOrDigit(input.charAt(pos)) || input.charAt(pos) == '_')) {
      pos++;
    }
    String word = input.substring(start, pos);
    TokenType type = KEYWORDS.getOrDefault(word, TokenType.IDENTIFIER);
    return new Token(type, word, start);
  }
}
