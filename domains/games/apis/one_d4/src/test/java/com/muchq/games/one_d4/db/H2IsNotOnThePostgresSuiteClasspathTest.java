package com.muchq.games.one_d4.db;

import static org.assertj.core.api.Assertions.assertThatCode;
import static org.assertj.core.api.Assertions.assertThatThrownBy;

import org.junit.jupiter.api.Test;

/**
 * These suites talk to real Postgres via {@link PgTestUrls}. They must not carry the H2 driver —
 * that is what makes "H2 is for the TestDb suites" a fact about the build graph rather than a
 * comment on nine identical {@code artifact} lines. Putting H2 on {@code :test_db} and leaving
 * {@code PgTestUrls} in the same library would reintroduce it here the moment this suite depends on
 * {@code :test_db} for the URL helper; the split is what this assertion guards.
 *
 * <p>The control is pgjdbc, which this suite's {@code runtime_deps} do carry — without it this
 * passes just as well against a lookup that can no longer find anything at all.
 */
public class H2IsNotOnThePostgresSuiteClasspathTest {

  @Test
  public void h2IsNotOnThePostgresSuiteClasspath() {
    assertThatThrownBy(() -> Class.forName("org.h2.Driver"))
        .as(
            "org.h2.Driver resolves from pg_db_tests. These suites run against Postgres; H2 belongs"
                + " on :test_db (and the module-boot e2e suites), not here.")
        .isInstanceOf(ClassNotFoundException.class);

    assertThatCode(() -> Class.forName("org.postgresql.Driver"))
        .as("pgjdbc is missing too, so the assertion above proves nothing")
        .doesNotThrowAnyException();
  }
}
