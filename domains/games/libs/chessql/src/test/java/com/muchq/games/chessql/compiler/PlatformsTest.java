package com.muchq.games.chessql.compiler;

import static org.assertj.core.api.Assertions.assertThat;

import java.util.Locale;
import org.junit.jupiter.api.Test;

public class PlatformsTest {

  @Test
  public void dottedNamesBecomeUnderscored() {
    assertThat(Platforms.canonical("chess.com")).isEqualTo("CHESS_COM");
  }

  @Test
  public void caseAndSurroundingSpaceDoNotMatter() {
    assertThat(Platforms.canonical("  Chess.Com ")).isEqualTo("CHESS_COM");
    assertThat(Platforms.canonical("lichess")).isEqualTo("LICHESS");
    assertThat(Platforms.canonical("LiChess")).isEqualTo("LICHESS");
  }

  @Test
  public void aCanonicalValueIsItsOwnCanonicalForm() {
    assertThat(Platforms.canonical("CHESS_COM")).isEqualTo("CHESS_COM");
    assertThat(Platforms.canonical("LICHESS")).isEqualTo("LICHESS");
  }

  /**
   * Unknown platforms canonicalise rather than throw. Rejecting one is the request path's job — a
   * filter naming a platform nobody indexed is a query that legitimately matches no rows, not a
   * 400.
   */
  @Test
  public void unknownPlatformsAreNormalizedNotRejected() {
    assertThat(Platforms.canonical("chess24.com")).isEqualTo("CHESS24_COM");
  }

  /**
   * Turkish maps 'i' to a dotless capital under a default-locale upper-case, so a JVM running there
   * would store LİCHESS and match nobody's query.
   */
  @Test
  public void upperCasingIsLocaleIndependent() {
    Locale previous = Locale.getDefault();
    try {
      Locale.setDefault(Locale.forLanguageTag("tr-TR"));
      assertThat(Platforms.canonical("lichess")).isEqualTo("LICHESS");
    } finally {
      Locale.setDefault(previous);
    }
  }
}
