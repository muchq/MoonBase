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

  /**
   * The move continuation belongs in the name — it is what makes opening_name the fine-grained
   * value that opening_family is the coarse counterpart of. The #1344 strip is scoped to
   * familyFromName; hoisting it up here would silently coarsen opening_name too.
   */
  @Test
  public void nameFromEcoUrl_keepsTheMoveContinuation() {
    assertThat(
            Openings.nameFromEcoUrl(
                "https://www.chess.com/openings/Owens-Defense...3.Nc3-e6-4.Nf3-Bb4"))
        .isEqualTo("Owens Defense...3.Nc3 e6 4.Nf3 Bb4");
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

  /**
   * #1344: chess.com glues the move continuation onto the *first* structural word, hiding it from
   * the terminator scan. Both halves of that failure are here — the raw moves leaking into the key
   * ("Owens Defense...3.Nc3"), and the family silently losing its structural word ("Giuoco Piano"
   * for "Giuoco Piano Game"), which is the worse one: it splits a single family across two
   * opening_family group keys, so per-family counts and win rates are wrong with nothing in the
   * output to show it. The last case is the corpus's castling shape, where "O-O" despaces into "O
   * O" and the two-word fallback cut mid-continuation ("Pirc Defense...6.O").
   */
  @Test
  public void familyFromName_stripsAMoveContinuationOnTheFirstStructuralWord() {
    assertThat(Openings.familyFromName("Owens Defense...3.Nc3 e6 4.Nf3 Bb4"))
        .isEqualTo("Owens Defense");
    assertThat(Openings.familyFromName("Giuoco Piano Game...5.d3 d6 6.c3 O O 7.Re1"))
        .isEqualTo("Giuoco Piano Game");
    assertThat(Openings.familyFromName("Kings Indian Attack...3.Bg2 e5 4.d3 Nf6 5.O O"))
        .isEqualTo("Kings Indian Attack");
    assertThat(Openings.familyFromName("Pirc Defense...6.O O Bg7 7.h3 O O"))
        .isEqualTo("Pirc Defense");
  }

  /**
   * The shape that already worked before #1344 — the continuation rides a later word, so the scan
   * cut before ever reaching it. It is why OpeningsTest passed while real months were mis-bucketed,
   * so it stays pinned alongside the shape that didn't.
   */
  @Test
  public void familyFromName_stripsAMoveContinuationOnALaterWord() {
    assertThat(Openings.familyFromName("Sicilian Defense Chekhover Variation...7.Nc3 Nf6 8.Bg5 e6"))
        .isEqualTo("Sicilian Defense");
    assertThat(Openings.familyFromName("Caro Kann Defense Two Knights Attack 3...dxe4"))
        .isEqualTo("Caro Kann Defense");
  }

  /**
   * Cut at the first continuation, not the last. chess.com writes a black-to-move continuation as
   * "4...Bb4", so a slug can carry a second "..." inside the moves, and cutting at the last one
   * leaves the earlier moves in the key. Nothing in the frozen corpus has two — this is the
   * mutation the corpus suite could not kill, pinned by hand rather than left to intent.
   */
  @Test
  public void familyFromName_cutsAtTheFirstOfSeveralMoveContinuations() {
    assertThat(Openings.familyFromName("Owens Defense...3.Nc3 e6 4...Bb4"))
        .isEqualTo("Owens Defense");
  }

  @Test
  public void familyFromName_fallsBackToFirstTwoWords() {
    assertThat(Openings.familyFromName("Something Unusual Line Here"))
        .isEqualTo("Something Unusual");
    assertThat(Openings.familyFromName("Singleword")).isEqualTo("Singleword");
  }

  /** The fallback counts words of the stripped name, not of the continuation. */
  @Test
  public void familyFromName_fallsBackToFirstTwoWordsOfTheStrippedName() {
    assertThat(Openings.familyFromName("Grob...2.Bg2 e5")).isEqualTo("Grob");
    assertThat(Openings.familyFromName("Something Unusual...2.Nf3 d5 3.g3"))
        .isEqualTo("Something Unusual");
  }

  @Test
  public void familyFromName_nullOrBlankYieldsNull() {
    assertThat(Openings.familyFromName(null)).isNull();
    assertThat(Openings.familyFromName(" ")).isNull();
  }

  /** Nothing survives the strip, so there is no family — not an empty-string group key. */
  @Test
  public void familyFromName_moveContinuationOnlyYieldsNull() {
    assertThat(Openings.familyFromName("...3.Nc3 e6")).isNull();
    assertThat(Openings.familyFromName("  ...  ")).isNull();
  }
}
