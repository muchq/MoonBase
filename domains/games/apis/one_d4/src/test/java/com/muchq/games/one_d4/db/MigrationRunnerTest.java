package com.muchq.games.one_d4.db;

import static org.assertj.core.api.Assertions.assertThat;

import org.junit.jupiter.api.Test;

/**
 * The one-shot's exit contract, which {@code one_d4_worker}'s compose gate keys on: {@code
 * service_completed_successfully} reads the process exit code, so a wrong 0 here releases the
 * worker against a schema nobody migrated. The success path needs a real Postgres and lives in
 * {@code PostgresMigrationRunnerTest}.
 */
public class MigrationRunnerTest {

  @Test
  public void aMissingUrlIsExitOne() {
    assertThat(MigrationRunner.run(null, null, null)).isEqualTo(1);
  }

  /** What compose produces when the interpolated variable is absent from the host's .env. */
  @Test
  public void aBlankUrlIsExitOne() {
    assertThat(MigrationRunner.run("   ", null, null)).isEqualTo(1);
  }

  /**
   * A URL no driver answers fails pool construction, and pool construction is this container's
   * likeliest failure — it must take the logged exit-1 path, not escape as an uncaught stack trace.
   */
  @Test
  public void anUnresolvableUrlIsExitOne() {
    assertThat(MigrationRunner.run("jdbc:nosuchdriver:nowhere", null, null)).isEqualTo(1);
  }
}
