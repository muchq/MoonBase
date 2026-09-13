package com.muchq.games.chessql.compiler;

import java.util.Locale;

/**
 * The one spelling rule for the {@code platform} column, applied by the request path that stores a
 * value and by the compiler that queries one. The stored spelling is canonical, so a literal must
 * be canonicalised before it is compared — {@code lichess}, {@code Lichess} and {@code LICHESS} are
 * one value, and so are {@code chess.com} and {@code CHESS_COM}.
 */
public final class Platforms {

  private Platforms() {}

  /** Canonical form: trimmed, upper-cased, dots to underscores. {@code chess.com → CHESS_COM}. */
  public static String canonical(String platform) {
    return platform.strip().toUpperCase(Locale.ROOT).replace('.', '_');
  }
}
