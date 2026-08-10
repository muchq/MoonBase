package com.muchq.games.one_d4.docs;

import static org.assertj.core.api.Assertions.assertThat;

import com.muchq.games.chessql.compiler.SqlCompiler;
import java.io.IOException;
import java.io.InputStream;
import java.nio.charset.StandardCharsets;
import java.util.LinkedHashSet;
import java.util.Set;
import java.util.regex.Matcher;
import java.util.regex.Pattern;
import org.junit.jupiter.api.Test;

/**
 * CHESSQL.md is served verbatim to MCP clients as {@code chessql://reference} (#1326), which makes
 * its field and motif tables a third copy of what {@link SqlCompiler} accepts — after the compiler
 * itself and {@code query_chess_games}' description. Third copies drift; this one is checked.
 *
 * <p>That is not hypothetical. When this test was written the tool description was already missing
 * {@code zugzwang} and {@code overloaded_piece}, both of which the compiler accepts and the doc
 * lists, so a model reading only the description could not know they existed. Serving the doc
 * without pinning it would have added a second place to make the same mistake.
 *
 * <p>The direction that matters most is doc-missing-a-field: a caller who cannot discover a field
 * simply never uses it, silently. The reverse — a doc listing a field the compiler rejects — is
 * noisier but worse per occurrence, because the caller writes a query that fails. Both are
 * asserted, in both directions, as set equality.
 *
 * <p>Prose is deliberately not pinned. This asserts which names appear, never how they are
 * described, so the doc stays free to be rewritten as documentation.
 */
public class ChessQlReferenceTest {

  /** A leading `backticked` cell in a markdown table row, i.e. the first column of each row. */
  private static final Pattern FIRST_COLUMN = Pattern.compile("(?m)^\\|\\s*`([^`]+)`");

  private static final Pattern MOTIF_CALL = Pattern.compile("`motif\\(([a-z_]+)\\)`");

  @Test
  public void theFieldTableListsExactlyTheFieldsTheCompilerAccepts() throws IOException {
    assertThat(firstColumnOf(section("## Fields", "### Date scoping")))
        .as("CHESSQL.md's field table and SqlCompiler.filterableFields() must agree")
        .isEqualTo(SqlCompiler.filterableFields());
  }

  @Test
  public void thePerspectiveTableListsExactlyThePerspectiveFields() throws IOException {
    assertThat(firstColumnOf(section("### Perspective fields", "## Motifs")))
        .as("CHESSQL.md's perspective table and SqlCompiler.perspectiveFields() must agree")
        .isEqualTo(SqlCompiler.perspectiveFields());
  }

  /**
   * Both motif tables at once — stored and ATTACK-derived. The split between them is an
   * implementation detail of how the predicate compiles; to a caller writing {@code motif(x)} it is
   * one roster, and it is the roster that has to be complete.
   */
  @Test
  public void theMotifTablesListExactlyTheMotifsTheCompilerAccepts() throws IOException {
    Set<String> documented = new LinkedHashSet<>();
    Matcher matcher = MOTIF_CALL.matcher(section("## Motifs", null));
    while (matcher.find()) {
      documented.add(matcher.group(1));
    }

    assertThat(documented)
        .as("CHESSQL.md's motif tables and SqlCompiler.motifs() must agree")
        .isEqualTo(SqlCompiler.motifs());
  }

  /**
   * The control. Every assertion above is a set comparison against text carved out by heading, so a
   * renamed heading would silently compare against an empty section and the roster tests would fail
   * for a confusing reason — or, if the compiler roster were ever empty too, pass while checking
   * nothing.
   */
  @Test
  public void theSectionsThisTestReadsExist() throws IOException {
    assertThat(section("## Fields", "### Date scoping")).isNotBlank();
    assertThat(section("### Perspective fields", "## Motifs")).isNotBlank();
    assertThat(section("## Motifs", null)).isNotBlank();
    assertThat(SqlCompiler.filterableFields()).isNotEmpty();
    assertThat(SqlCompiler.perspectiveFields()).isNotEmpty();
    assertThat(SqlCompiler.motifs()).isNotEmpty();
  }

  private static Set<String> firstColumnOf(String markdown) {
    Set<String> names = new LinkedHashSet<>();
    Matcher matcher = FIRST_COLUMN.matcher(markdown);
    while (matcher.find()) {
      names.add(matcher.group(1));
    }
    return names;
  }

  /** The text between two headings, {@code end} null meaning "to the end of the document". */
  private static String section(String start, String end) throws IOException {
    String doc = reference();
    int from = doc.indexOf(start);
    assertThat(from).as("CHESSQL.md must contain the heading %s", start).isNotNegative();
    int to = end == null ? doc.length() : doc.indexOf(end, from);
    assertThat(to)
        .as("CHESSQL.md must contain the heading %s after %s", end, start)
        .isNotNegative();
    return doc.substring(from, to);
  }

  private static String reference() throws IOException {
    try (InputStream in = ChessQlReferenceTest.class.getResourceAsStream("/CHESSQL.md")) {
      assertThat(in).as("CHESSQL.md must be on the test classpath").isNotNull();
      return new String(in.readAllBytes(), StandardCharsets.UTF_8);
    }
  }
}
