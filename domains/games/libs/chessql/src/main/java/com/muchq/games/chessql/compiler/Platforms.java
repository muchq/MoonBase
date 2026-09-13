package com.muchq.games.chessql.compiler;

import java.util.Locale;

/**
 * The one spelling rule for the {@code platform} column, shared by the writer that stores it and
 * the compiler that queries it.
 *
 * <p>They have to agree, and for a while they did not: the request path stored {@code CHESS_COM}
 * while a {@code platform = "chess.com"} filter bound {@code chess.com}, so the obvious query
 * returned nothing (#1539). Canonicalising in one place is what keeps a new platform from
 * re-opening that gap — {@code lichess}, {@code Lichess} and {@code LICHESS} are one value.
 */
public final class Platforms {

  private Platforms() {}

  /** Canonical form: trimmed, upper-cased, dots to underscores. {@code chess.com → CHESS_COM}. */
  public static String canonical(String platform) {
    return platform.strip().toUpperCase(Locale.ROOT).replace('.', '_');
  }
}
