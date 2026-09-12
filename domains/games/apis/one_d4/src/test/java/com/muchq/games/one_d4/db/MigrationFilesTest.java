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
    assertThat(MigrationFiles.steps(FIXTURE)).containsExactly("V001__first", "V002__second");
  }

  @Test
  public void aStepResolvesToItsFile() {
    assertThat(MigrationFiles.sqlFor(FIXTURE, "V001__first")).contains("the first step");
    assertThat(MigrationFiles.sqlFor(FIXTURE, "V002__second")).contains("the second step");
  }

  /** A listed step with no SQL must fail the migration, not skip. */
  @Test
  public void aListedStepWithNoFileIsAnError() {
    assertThatThrownBy(() -> MigrationFiles.sqlFor("migrations_fixture/missing", "V001__gone"))
        .isInstanceOf(IllegalStateException.class)
        .hasMessageContaining("V001__gone");
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
   * Every real step resolves, which is what makes a manifest line and a file the same fact: a step
   * listed without its .sql (or shipped without its BUILD entry) fails here rather than at deploy.
   */
  @Test
  public void everyRealStepResolves() {
    for (String step : MigrationFiles.steps()) {
      assertThat(MigrationFiles.sqlFor(step)).as("step %s", step).isNotBlank();
    }
  }

  /** Every real file splits into at least one executable statement — no file is dead weight. */
  @Test
  public void everyRealStepCarriesAtLeastOneStatement() {
    for (String step : MigrationFiles.steps()) {
      assertThat(SqlStatements.split(MigrationFiles.sqlFor(step))).as("step %s", step).isNotEmpty();
    }
  }
}
