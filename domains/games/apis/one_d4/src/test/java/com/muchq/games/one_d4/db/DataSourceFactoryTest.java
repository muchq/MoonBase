package com.muchq.games.one_d4.db;

import static org.assertj.core.api.Assertions.assertThat;

import com.zaxxer.hikari.HikariConfig;
import org.junit.jupiter.api.Test;

public class DataSourceFactoryTest {

  @Test
  public void postgresUrlWithoutSocketTimeoutGetsTheDefault() {
    assertThat(DataSourceFactory.defaultSocketTimeout("jdbc:postgresql://host:5432/db?user=u"))
        .hasValue(150);
  }

  /**
   * Through the config create() actually builds, not just the policy function — deleting the
   * addDataSourceProperty wiring must fail here, not stay green behind a correct-but-unapplied
   * policy.
   */
  @Test
  public void createWiresTheDefaultIntoTheDriverProperties() {
    assertThat(
            DataSourceFactory.hikariConfig("jdbc:postgresql://host:5432/db?user=u")
                .getDataSourceProperties()
                .getProperty("socketTimeout"))
        .isEqualTo("150");
    assertThat(
            DataSourceFactory.hikariConfig("jdbc:h2:mem:x;DB_CLOSE_DELAY=-1")
                .getDataSourceProperties()
                .getProperty("socketTimeout"))
        .as("H2 must get no driver property it would reject")
        .isNull();
  }

  @Test
  public void explicitSocketTimeoutInTheUrlWins() {
    assertThat(
            DataSourceFactory.defaultSocketTimeout(
                "jdbc:postgresql://host:5432/db?user=u&socketTimeout=30"))
        .as("an operator's explicit choice in the URL must never be overridden")
        .isEmpty();
    assertThat(
            DataSourceFactory.defaultSocketTimeout(
                "jdbc:postgresql://host:5432/db?socketTimeout=30"))
        .as("first query parameter position counts too")
        .isEmpty();
  }

  @Test
  public void theSubstringInsideAnotherParametersValueDoesNotSuppressTheDefault() {
    // The "already set?" check must match at a parameter boundary. A bare substring search
    // would read this ApplicationName value as the operator choosing a socket timeout and
    // silently ship the connection without one.
    assertThat(
            DataSourceFactory.defaultSocketTimeout(
                "jdbc:postgresql://host:5432/db?user=u&ApplicationName=why_socketTimeout=is_here"))
        .hasValue(150);
  }

  @Test
  public void h2UrlsGetNoSocketTimeout() {
    // H2 rejects unknown connection properties outright, so the default must be Postgres-only.
    assertThat(DataSourceFactory.defaultSocketTimeout("jdbc:h2:mem:x;DB_CLOSE_DELAY=-1")).isEmpty();
  }

  /**
   * A long-running DELETE sends nothing over the socket until it finishes, so the driver-level
   * socket timeout must exceed the retention sweep's statement bound — otherwise a healthy
   * connection is severed mid-sweep before the server-side cancel fires. This is the ordering the
   * two constants' javadocs promise each other.
   */
  @Test
  public void socketTimeoutExceedsTheLongestSilentStatementBound() {
    assertThat(DataSourceFactory.PG_SOCKET_TIMEOUT_SECONDS)
        .isGreaterThan(StatementTimeouts.RETENTION_SWEEP_SECONDS);
  }

  /**
   * Credentials reach the driver as connection properties, not as URL text, so the secret's
   * alphabet stops mattering. pgjdbc URL-decodes query parameter values: the same password in
   * {@code ?password=} arrives as "a b" for {@code a+b}, truncates at {@code &}, and rewrites
   * {@code %41} to {@code A}. These are the exact characters that would be corrupted, asserted
   * through the config create() builds rather than through the policy alone.
   */
  @Test
  public void credentialsPassedBesideTheUrlSurviveCharactersAQueryStringWouldMangle() {
    for (String password : new String[] {"a+b", "a&b", "ab%41cd", "ab%zz", "plain"}) {
      HikariConfig config =
          DataSourceFactory.hikariConfig(
              "jdbc:postgresql://one_d4_postgres:5432/one_d4", "one_d4", password);
      assertThat(config.getPassword()).isEqualTo(password);
      assertThat(config.getUsername()).isEqualTo("one_d4");
      assertThat(config.getJdbcUrl())
          .as("the URL must stay free of credentials, or Hikari's masking has nothing to mask")
          .doesNotContain(password)
          .doesNotContain("password=");
    }
  }

  /**
   * The compatibility half. A URL that carries its own credentials — /etc/one_d4/db_config, and
   * every H2 test URL — must be untouched, so unset variables cannot override what the URL says.
   */
  @Test
  public void absentCredentialsLeaveTheConfigAlone() {
    for (String[] pair : new String[][] {{null, null}, {"", ""}}) {
      HikariConfig config =
          DataSourceFactory.hikariConfig("jdbc:h2:mem:x;DB_CLOSE_DELAY=-1", pair[0], pair[1]);
      assertThat(config.getUsername()).isNull();
      assertThat(config.getPassword()).isNull();
    }
  }
}
