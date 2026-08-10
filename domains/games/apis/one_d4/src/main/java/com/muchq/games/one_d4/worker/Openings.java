package com.muchq.games.one_d4.worker;

import java.util.Locale;
import java.util.Set;
import java.util.regex.Pattern;
import org.jspecify.annotations.Nullable;

/**
 * Derives human-readable opening names from the chess.com {@code ECOUrl} slug (surfaced as the
 * {@code eco} field on monthly-archive games), e.g. {@code
 * https://www.chess.com/openings/Caro-Kann-Defense-Two-Knights-Attack-3...dxe4}.
 *
 * <p>The family derivation is a deliberately naive v1: it drops the move continuation, then keeps
 * slug words up to and including the first structural word (Defense, Opening, Gambit, ...), falling
 * back to the first two words. That maps "English-Opening-Agincourt-Defense-2.Nf3-d5-3.g3" to
 * "English Opening", "Caro-Kann-Defense-Two-Knights-Attack" to "Caro Kann Defense", and
 * "Owens-Defense...3.Nc3-e6" to "Owens Defense", which is the level most questions are asked at.
 */
public final class Openings {

  private static final int MAX_LENGTH = 255;
  private static final Pattern ECO_CODE = Pattern.compile("[A-E]\\d{2}[a-z]?");
  private static final Set<String> FAMILY_TERMINATORS =
      Set.of("opening", "defense", "defence", "game", "attack", "gambit", "system", "variation");

  private Openings() {}

  /**
   * Extracts the opening name from an ECOUrl, or null when the value is missing or carries no name
   * (e.g. a bare ECO code like "B10").
   */
  public static @Nullable String nameFromEcoUrl(@Nullable String ecoUrl) {
    if (ecoUrl == null || ecoUrl.isBlank()) {
      return null;
    }
    String trimmed = ecoUrl.strip();
    while (trimmed.endsWith("/")) {
      trimmed = trimmed.substring(0, trimmed.length() - 1);
    }
    String slug = trimmed.substring(trimmed.lastIndexOf('/') + 1);
    // "openings" is the bare path directory (https://www.chess.com/openings/) — no slug present.
    if (slug.isBlank() || slug.equalsIgnoreCase("openings") || ECO_CODE.matcher(slug).matches()) {
      return null;
    }
    String name = slug.replace('-', ' ').strip();
    if (name.isBlank()) {
      return null;
    }
    return truncate(name);
  }

  /**
   * Derives the opening family (e.g. "Caro Kann Defense") from a full opening name, or null when no
   * name is available or the name is nothing but a move continuation.
   */
  public static @Nullable String familyFromName(@Nullable String openingName) {
    if (openingName == null) {
      return null;
    }
    // Covers a blank name too: stripping leaves nothing either way.
    String base = stripMoveContinuation(openingName);
    if (base.isEmpty()) {
      return null;
    }
    String[] words = base.split("\\s+");
    for (int i = 0; i < words.length; i++) {
      if (FAMILY_TERMINATORS.contains(words[i].toLowerCase(Locale.ROOT))) {
        return truncate(String.join(" ", java.util.Arrays.copyOfRange(words, 0, i + 1)));
      }
    }
    int take = Math.min(2, words.length);
    return truncate(String.join(" ", java.util.Arrays.copyOfRange(words, 0, take)));
  }

  /**
   * Drops chess.com's move continuation — everything from the first "..." — so the terminator scan
   * sees opening words only. The continuation is glued straight onto the preceding word
   * ("Owens-Defense...3.Nc3-e6"), which is what hides that word from a scan that splits on
   * whitespace.
   */
  private static String stripMoveContinuation(String openingName) {
    int continuation = openingName.indexOf("...");
    return (continuation < 0 ? openingName : openingName.substring(0, continuation)).strip();
  }

  private static String truncate(String value) {
    return value.length() <= MAX_LENGTH ? value : value.substring(0, MAX_LENGTH);
  }
}
