package com.muchq.games.one_d4.db;

import java.util.ArrayList;
import java.util.List;

/**
 * Splits a migration file into the statements {@link Migration} executes, so each file is plain
 * multi-statement SQL that {@code psql -f} would accept while the JDBC path still runs one
 * statement per {@code execute} — exactly what it ran when the DDL was Java string constants.
 *
 * <p>Splits on top-level semicolons only: semicolons inside single-quoted strings, line and block
 * comments (nested, as Postgres nests them), and dollar-quoted bodies — the {@code DO $$ ... END
 * $$} blocks are why this class exists — are body, not terminators. Comments travel with the
 * statement that follows them; a chunk that is only comments or whitespace is not a statement. A
 * final statement missing its terminator is kept, because dropping it silently would mean a
 * migration file whose last statement never runs.
 */
public final class SqlStatements {

  private SqlStatements() {}

  public static List<String> split(String sql) {
    List<String> statements = new ArrayList<>();
    StringBuilder current = new StringBuilder();
    boolean executable = false;
    int i = 0;
    int n = sql.length();
    while (i < n) {
      char c = sql.charAt(i);
      if (c == '-' && i + 1 < n && sql.charAt(i + 1) == '-') {
        int end = sql.indexOf('\n', i);
        end = end < 0 ? n : end;
        current.append(sql, i, end);
        i = end;
      } else if (c == '/' && i + 1 < n && sql.charAt(i + 1) == '*') {
        int end = blockCommentEnd(sql, i);
        current.append(sql, i, end);
        i = end;
      } else if (c == '\'') {
        int end = stringEnd(sql, i);
        current.append(sql, i, end);
        executable = true;
        i = end;
      } else if (c == '$') {
        int tagEnd = dollarTagEnd(sql, i);
        if (tagEnd < 0) {
          current.append(c);
          executable = true;
          i++;
        } else {
          String tag = sql.substring(i, tagEnd);
          int close = sql.indexOf(tag, tagEnd);
          if (close < 0) {
            throw new IllegalArgumentException("unterminated dollar-quote " + tag);
          }
          current.append(sql, i, close + tag.length());
          executable = true;
          i = close + tag.length();
        }
      } else if (c == ';') {
        if (executable) {
          statements.add(current.toString().strip());
        }
        current.setLength(0);
        executable = false;
        i++;
      } else {
        current.append(c);
        if (!Character.isWhitespace(c)) {
          executable = true;
        }
        i++;
      }
    }
    if (executable) {
      statements.add(current.toString().strip());
    }
    return statements;
  }

  /** Index just past the comment's closing star-slash. Postgres block comments nest. */
  private static int blockCommentEnd(String sql, int start) {
    int depth = 0;
    int i = start;
    while (i + 1 < sql.length()) {
      if (sql.charAt(i) == '/' && sql.charAt(i + 1) == '*') {
        depth++;
        i += 2;
      } else if (sql.charAt(i) == '*' && sql.charAt(i + 1) == '/') {
        depth--;
        i += 2;
        if (depth == 0) {
          return i;
        }
      } else {
        i++;
      }
    }
    throw new IllegalArgumentException("unterminated block comment");
  }

  /** Index just past the closing quote. A doubled quote inside is an escape, not a close. */
  private static int stringEnd(String sql, int start) {
    int i = start + 1;
    while (i < sql.length()) {
      if (sql.charAt(i) != '\'') {
        i++;
      } else if (i + 1 < sql.length() && sql.charAt(i + 1) == '\'') {
        i += 2;
      } else {
        return i + 1;
      }
    }
    throw new IllegalArgumentException("unterminated quote");
  }

  /**
   * If {@code start} opens a dollar-quote tag ({@code $$} or {@code $tag$}), the index just past
   * the opener; otherwise -1 and the dollar sign is plain text. A tag follows Postgres's
   * unquoted-identifier rules — it cannot begin with a digit, so {@code $1$} is a positional
   * parameter followed by text, not an opener.
   */
  private static int dollarTagEnd(String sql, int start) {
    int i = start + 1;
    if (i < sql.length() && Character.isDigit(sql.charAt(i))) {
      return -1;
    }
    while (i < sql.length() && (Character.isLetterOrDigit(sql.charAt(i)) || sql.charAt(i) == '_')) {
      i++;
    }
    return i < sql.length() && sql.charAt(i) == '$' ? i + 1 : -1;
  }
}
