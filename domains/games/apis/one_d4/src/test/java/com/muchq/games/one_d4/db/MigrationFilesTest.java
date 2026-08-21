package com.muchq.games.one_d4.db;

import static org.assertj.core.api.Assertions.assertThat;
import static org.assertj.core.api.Assertions.assertThatThrownBy;

import java.util.List;
import java.util.regex.Pattern;
import org.junit.jupiter.api.Test;

public class MigrationFilesTest {

  // A miniature migrations tree under src/test/resources, so the resolution
  // failure modes can be exercised without breaking the real tree.
  private static final String FIXTURE = "migrations_fixture/good";

  @Test
  public void manifestOrderIsFileOrder_commentsAndBlanksIgnored() {
    assertThat(MigrationFiles.steps(FIXTURE)).containsExactly("V001__forked", "V002__shared");
  }

  @Test
  public void forkedStepResolvesToTheEngineFile() {
    assertThat(MigrationFiles.sqlFor(FIXTURE, "V001__forked", "pg")).contains("pg side");
    assertThat(MigrationFiles.sqlFor(FIXTURE, "V001__forked", "h2")).contains("h2 side");
  }

  @Test
  public void sharedStepResolvesToTheTopLevelFile() {
    assertThat(MigrationFiles.sqlFor(FIXTURE, "V002__shared", "pg"))
        .contains("shared for every engine");
    assertThat(MigrationFiles.sqlFor(FIXTURE, "V002__shared", "h2"))
        .contains("shared for every engine");
  }

  /**
   * A step with both a shared and an engine file is two sources of truth for one engine — refusing
   * to run beats silently picking one.
   */
  @Test
  public void aStepWithBothSharedAndEngineFilesIsAnError() {
    assertThatThrownBy(
            () -> MigrationFiles.sqlFor("migrations_fixture/ambiguous", "V001__both", "pg"))
        .isInstanceOf(IllegalStateException.class)
        .hasMessageContaining("V001__both")
        .hasMessageContaining("both");
  }

  /** A listed step with no SQL for this engine must fail the migration, not skip. */
  @Test
  public void aStepWithNoFileForTheEngineIsAnError() {
    assertThatThrownBy(
            () -> MigrationFiles.sqlFor("migrations_fixture/missing", "V001__gone", "pg"))
        .isInstanceOf(IllegalStateException.class)
        .hasMessageContaining("V001__gone")
        .hasMessageContaining("no SQL");
  }

  @Test
  public void aMissingManifestIsAnError() {
    assertThatThrownBy(() -> MigrationFiles.steps("migrations_fixture/nowhere"))
        .isInstanceOf(IllegalStateException.class)
        .hasMessageContaining("manifest");
  }

  // The real tree, held to its own rules.

  @Test
  public void realManifestStepsAreWellFormedAndContiguouslyNumbered() {
    List<String> steps = MigrationFiles.steps();
    assertThat(steps).isNotEmpty();
    Pattern name = Pattern.compile("V(\\d{3})__[a-z0-9_]+");
    for (int i = 0; i < steps.size(); i++) {
      var matcher = name.matcher(steps.get(i));
      assertThat(matcher.matches()).as("step name %s", steps.get(i)).isTrue();
      assertThat(Integer.parseInt(matcher.group(1)))
          .as(
              "steps must be numbered contiguously from V001, so two branches adding a step"
                  + " collide in the manifest instead of silently interleaving")
          .isEqualTo(i + 1);
    }
  }

  /**
   * Every real step resolves to exactly one file on both engines. This is what turns "the migration
   * path stays identical on both engines" from a convention into a failure: a Postgres-only step
   * with no H2 side (or the reverse) fails here, on the ordinary H2-only CI run.
   */
  @Test
  public void everyRealStepResolvesForBothEngines() {
    for (String step : MigrationFiles.steps()) {
      for (String engine : new String[] {"pg", "h2"}) {
        assertThat(MigrationFiles.sqlFor(step, engine))
            .as("step %s on %s", step, engine)
            .isNotBlank();
      }
    }
  }

  /** Every real file splits into at least one executable statement — no file is dead weight. */
  @Test
  public void everyRealStepCarriesAtLeastOneStatement() {
    for (String step : MigrationFiles.steps()) {
      for (String engine : new String[] {"pg", "h2"}) {
        assertThat(SqlStatements.split(MigrationFiles.sqlFor(step, engine)))
            .as("step %s on %s", step, engine)
            .isNotEmpty();
      }
    }
  }
}
