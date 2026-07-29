package com.muchq.games.one_d4.worker;

import static org.assertj.core.api.Assertions.assertThat;

import org.junit.jupiter.api.Test;

public class OpeningsTest {

  @Test
  public void nameFromEcoUrl_extractsSlugAsSpacedName() {
    assertThat(
            Openings.nameFromEcoUrl(
                "https://www.chess.com/openings/Caro-Kann-Defense-Two-Knights-Attack-3...dxe4"))
        .isEqualTo("Caro Kann Defense Two Knights Attack 3...dxe4");
    assertThat(
            Openings.nameFromEcoUrl(
                "https://www.chess.com/openings/English-Opening-Agincourt-Defense-2.Nf3-d5-3.g3"))
        .isEqualTo("English Opening Agincourt Defense 2.Nf3 d5 3.g3");
  }

  @Test
  public void nameFromEcoUrl_toleratesTrailingSlash() {
    assertThat(Openings.nameFromEcoUrl("https://www.chess.com/openings/Vienna-Game/"))
        .isEqualTo("Vienna Game");
  }

  @Test
  public void nameFromEcoUrl_bareEcoCodeYieldsNull() {
    assertThat(Openings.nameFromEcoUrl("B10")).isNull();
    assertThat(Openings.nameFromEcoUrl("https://www.chess.com/openings/B10")).isNull();
  }

  @Test
  public void nameFromEcoUrl_nullOrBlankYieldsNull() {
    assertThat(Openings.nameFromEcoUrl(null)).isNull();
    assertThat(Openings.nameFromEcoUrl("  ")).isNull();
    assertThat(Openings.nameFromEcoUrl("https://www.chess.com/openings/")).isNull();
  }

  @Test
  public void familyFromName_stopsAtFirstStructuralWord() {
    assertThat(Openings.familyFromName("English Opening Agincourt Defense 2.Nf3 d5 3.g3"))
        .isEqualTo("English Opening");
    assertThat(Openings.familyFromName("Caro Kann Defense Two Knights Attack 3...dxe4"))
        .isEqualTo("Caro Kann Defense");
    assertThat(Openings.familyFromName("Kings Indian Attack")).isEqualTo("Kings Indian Attack");
    assertThat(Openings.familyFromName("Queens Gambit Declined Modern Variation"))
        .isEqualTo("Queens Gambit");
    assertThat(Openings.familyFromName("London System")).isEqualTo("London System");
  }

  @Test
  public void familyFromName_fallsBackToFirstTwoWords() {
    assertThat(Openings.familyFromName("Something Unusual Line Here"))
        .isEqualTo("Something Unusual");
    assertThat(Openings.familyFromName("Singleword")).isEqualTo("Singleword");
  }

  @Test
  public void familyFromName_nullOrBlankYieldsNull() {
    assertThat(Openings.familyFromName(null)).isNull();
    assertThat(Openings.familyFromName(" ")).isNull();
  }
}
