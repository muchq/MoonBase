package com.muchq.games.one_d4.docs;

import static org.assertj.core.api.Assertions.assertThat;

import com.muchq.games.chessql.compiler.CompiledQuery;
import com.muchq.games.chessql.compiler.SqlCompiler;
import com.muchq.games.chessql.parser.Parser;
import java.io.IOException;
import java.io.InputStream;
import java.nio.charset.StandardCharsets;
import java.util.ArrayList;
import java.util.LinkedHashSet;
import java.util.List;
import java.util.Optional;
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
   *
   * <p>Bounded at {@code ## Values}, not run to the end of the document. Review caught this: the
   * Compilation Examples table further down also writes {@code `motif(pin)`} and {@code
   * `motif(checkmate)`}, so an unbounded scan let those two names be re-supplied from outside the
   * roster — deleting either row from the tables above left this test green while the served
   * reference no longer documented the motif anywhere a reader would look.
   */
  @Test
  public void theMotifTablesListExactlyTheMotifsTheCompilerAccepts() throws IOException {
    Set<String> documented = new LinkedHashSet<>();
    Matcher matcher = MOTIF_CALL.matcher(section("## Motifs", "## Values"));
    while (matcher.find()) {
      documented.add(matcher.group(1));
    }

    assertThat(documented)
        .as("CHESSQL.md's motif tables and SqlCompiler.motifs() must agree")
        .isEqualTo(SqlCompiler.motifs());
  }

  /**
   * The Compilation Examples table, compiled. Every other assertion here pins which names appear;
   * this one pins what the doc claims the compiler <em>emits</em>, which is the part that had no
   * check at all — #1302 changed the SQL for negation and left the table describing the old shape,
   * green, in a file served verbatim to MCP clients as the query-language reference.
   *
   * <p>The documented fragment is matched as an ordered set of substrings so a row may elide with
   * {@code ...}, which several do rather than reprint a whole EXISTS subquery.
   *
   * <p>Parameters are pinned only where the column is unambiguous — every entry a quoted string or
   * a bare integer. The timestamp rows render a bound value for a human rather than as its {@code
   * toString}, so they are skipped rather than given a parser that would only ever be approximately
   * right. The Parameters column is pinned at all because a bind can change without the SQL
   * changing — platform literals are canonicalised at compile time — and an unpinned column is one
   * a reader has no reason to distrust.
   */
  @Test
  public void theCompilationExamplesTableMatchesWhatTheCompilerEmits() throws IOException {
    SqlCompiler compiler = new SqlCompiler();
    int checked = 0;
    int pinnedParameters = 0;

    for (String line : section("## Compilation Examples", "## Error Handling").split("\n")) {
      String[] cells = line.split("\\|");
      // A data row is `| `input` | `sql` | ... |`; the heading and separator rows have no
      // backticks.
      if (cells.length < 3 || !cells[1].trim().startsWith("`")) {
        continue;
      }
      String input = unbacktick(cells[1]);
      String documentedSql = unbacktick(cells[2]);
      CompiledQuery compiled = compiler.compile(Parser.parse(input));

      Optional<List<Object>> documented =
          cells.length > 3 ? documentedParameters(unbacktick(cells[3])) : Optional.empty();
      if (documented.isPresent()) {
        assertThat(compiled.parameters())
            .as("CHESSQL.md documents `%s` as binding %s", input, documented.get())
            .isEqualTo(documented.get());
        pinnedParameters++;
      }

      String actual = compiled.selectSql();
      String cursor = actual;
      for (String fragment : documentedSql.split("\\.\\.\\.")) {
        assertThat(cursor)
            .as(
                "CHESSQL.md documents `%s` as compiling to `%s`, but it emits: %s",
                input, documentedSql, actual)
            .contains(fragment);
        cursor = cursor.substring(cursor.indexOf(fragment) + fragment.length());
      }
      checked++;
    }

    assertThat(checked)
        .as("the Compilation Examples table must still have rows to check")
        .isGreaterThanOrEqualTo(8);
    // An unreadable Parameters cell is skipped, so a reformat could quietly un-pin every row and
    // leave this test green on a table it no longer reads.
    assertThat(pinnedParameters)
        .as("the Compilation Examples table must still have readable Parameters cells")
        .isGreaterThanOrEqualTo(8);
  }

  /** A rendered bound timestamp — the one entry shape this deliberately does not read back. */
  private static final Pattern RENDERED_TIMESTAMP = Pattern.compile("\\d{4}-\\d{2}-\\d{2}T\\S*");

  /**
   * The Parameters cell as values, or empty for the timestamp rows, which render a bound value for
   * a human rather than as its {@code toString}.
   *
   * <p>Only that shape is skipped. Anything else unreadable fails: quietly skipping it would let a
   * single edit both un-pin a row and change what it documents, and the floor on the number of
   * pinned rows cannot see which row went missing.
   */
  private static Optional<List<Object>> documentedParameters(String cell) {
    if (!cell.startsWith("[") || !cell.endsWith("]")) {
      return Optional.empty();
    }
    String body = cell.substring(1, cell.length() - 1).trim();
    if (body.isEmpty()) {
      return Optional.of(List.of());
    }
    List<Object> values = new ArrayList<>();
    for (String entry : body.split(",")) {
      String trimmed = entry.trim();
      if (trimmed.length() > 1 && trimmed.startsWith("\"") && trimmed.endsWith("\"")) {
        values.add(trimmed.substring(1, trimmed.length() - 1));
      } else if (trimmed.matches("-?\\d+")) {
        values.add(Integer.valueOf(trimmed));
      } else if (RENDERED_TIMESTAMP.matcher(trimmed).matches()) {
        return Optional.empty();
      } else {
        throw new AssertionError(
            "CHESSQL.md Parameters cell "
                + cell
                + " has an entry this cannot read: "
                + trimmed
                + " — quote a string, leave an integer bare, or it is not a Parameters cell");
      }
    }
    return Optional.of(values);
  }

  private static String unbacktick(String cell) {
    return cell.trim().replaceAll("^`|`$", "");
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
    assertThat(section("## Motifs", "## Values")).isNotBlank();
    // The bound that keeps Compilation Examples out of the motif roster. If this heading is
    // renamed, section() fails loudly here rather than the scan silently widening to EOF.
    assertThat(reference()).contains("## Values");
    assertThat(section("## Compilation Examples", "## Error Handling")).isNotBlank();
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
