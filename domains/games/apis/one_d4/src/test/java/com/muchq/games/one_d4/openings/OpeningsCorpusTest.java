package com.muchq.games.one_d4.openings;

import static org.assertj.core.api.Assertions.assertThat;

import java.io.IOException;
import java.io.InputStream;
import java.nio.charset.StandardCharsets;
import java.util.ArrayList;
import java.util.LinkedHashSet;
import java.util.List;
import java.util.Set;
import java.util.regex.Matcher;
import java.util.regex.Pattern;
import org.junit.jupiter.api.Test;

/**
 * Replays every [ECOUrl] in the frozen 500-game corpus (see src/test/resources/hikaru_corpus.pgn)
 * through the production derivation. OpeningsTest covers the slug shapes we thought of; this covers
 * the ones chess.com actually emits, which is the difference that let #1344 ship — the hand-written
 * "..." case happened to be the shape that already worked, while 21 of the 307 distinct slugs
 * sitting in this repo's own corpus derived a wrong family.
 *
 * <p>The invariant is the interesting half: a family key is opening words only, so no move
 * continuation may survive into one. Digits and '.' are the tell, since no opening name carries
 * either. The distinct-family count is the positive control — the corpus is frozen, so a derivation
 * that stops collapsing continuations (52 keys before the fix, 40 after) moves it even if every
 * individual key still looks clean.
 */
public class OpeningsCorpusTest {

  private static final Pattern ECO_URL = Pattern.compile("\\[ECOUrl \"([^\"]+)\"\\]");
  private static final int EXPECTED_ECO_URLS = 500;
  private static final int EXPECTED_DISTINCT_SLUGS = 307;
  private static final int EXPECTED_DISTINCT_FAMILIES = 40;

  @Test
  public void noCorpusFamilyCarriesAMoveContinuation() throws IOException {
    List<String> ecoUrls = loadEcoUrls();
    assertThat(ecoUrls).hasSize(EXPECTED_ECO_URLS);
    assertThat(Set.copyOf(ecoUrls)).hasSize(EXPECTED_DISTINCT_SLUGS);

    Set<String> families = new LinkedHashSet<>();
    for (String ecoUrl : ecoUrls) {
      String family = familyOf(ecoUrl);
      assertThat(family)
          .as("family for %s is opening words only", ecoUrl)
          .doesNotContain(".")
          .doesNotContainPattern("\\d");
      families.add(family);
    }

    assertThat(families).hasSize(EXPECTED_DISTINCT_FAMILIES);
  }

  /**
   * The two collapses #1344 reported, proven on the corpus rather than on a hand-typed name: each
   * family is present under its full name and absent under the truncation the old fallback
   * produced, so a regression shows up as two group keys where there should be one.
   */
  @Test
  public void corpusFamiliesKeepTheirStructuralWord() throws IOException {
    Set<String> families = new LinkedHashSet<>();
    for (String ecoUrl : loadEcoUrls()) {
      families.add(familyOf(ecoUrl));
    }

    assertThat(families)
        .contains("Giuoco Piano Game", "Kings Indian Attack", "Old Benoni Defense", "Owens Defense")
        .doesNotContain("Giuoco Piano", "Kings Indian", "Old Benoni");
  }

  /** Every corpus slug carries a name and a family; a null from either is a failure, not a skip. */
  private static String familyOf(String ecoUrl) {
    String name = Openings.nameFromEcoUrl(ecoUrl);
    assertThat(name).as("name for %s", ecoUrl).isNotNull();
    String family = Openings.familyFromName(name);
    assertThat(family).as("family for %s", ecoUrl).isNotNull();
    return family;
  }

  private static List<String> loadEcoUrls() throws IOException {
    try (InputStream in = OpeningsCorpusTest.class.getResourceAsStream("/hikaru_corpus.pgn")) {
      assertThat(in).as("hikaru_corpus.pgn on test classpath").isNotNull();
      Matcher matcher = ECO_URL.matcher(new String(in.readAllBytes(), StandardCharsets.UTF_8));
      List<String> ecoUrls = new ArrayList<>();
      while (matcher.find()) {
        ecoUrls.add(matcher.group(1));
      }
      return List.copyOf(ecoUrls);
    }
  }
}
