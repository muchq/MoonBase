package com.muchq.games.one_d4.db;

import java.net.URI;
import java.net.URLEncoder;
import java.nio.charset.StandardCharsets;
import java.util.ArrayList;
import java.util.List;
import org.junit.jupiter.api.Assumptions;
import org.opentest4j.TestAbortedException;

/**
 * The scratch Postgres database every suite runs against: where its URL comes from, and the
 * libpq-to-pgjdbc conversion. pgjdbc does not accept credentials in the authority, so they move to
 * query params; each suite passes its own {@code currentSchema} so suites sharing the database
 * cannot collide.
 */
public final class PgTestUrls {

  /** The libpq-style URL CI exports: {@code postgresql://user:pass@host:port/db}. */
  public static final String DB_URL_ENV = "PG_TEST_DB_URL";

  /**
   * Set wherever a missing database is a broken job rather than a developer without one. A skip is
   * indistinguishable from a pass in a CI summary, which is the trap that kept H2 around (#1532):
   * these suites are the only thing exercising the schema now, so somewhere has to refuse to pass
   * vacuously. GitHub Actions sets CI on every runner.
   */
  public static final String REQUIRE = "CI";

  private PgTestUrls() {}

  /**
   * The configured database.
   *
   * @throws TestAbortedException when none is configured and none is required, which JUnit reports
   *     as a skip
   * @throws IllegalStateException when {@link #REQUIRE} is set, so a CI job cannot pass by skipping
   */
  public static String requireRawUrl() {
    return requireRawUrl(System.getenv(DB_URL_ENV), System.getenv(REQUIRE));
  }

  /** Split from the environment so {@code PgTestUrlsTest} can drive all three outcomes. */
  static String requireRawUrl(String rawUrl, String required) {
    if (rawUrl != null && !rawUrl.isBlank()) {
      return rawUrl;
    }
    if (required != null && !required.isBlank()) {
      throw new IllegalStateException(
          DB_URL_ENV
              + " is unset but "
              + REQUIRE
              + " is set. These suites are what exercises the schema; skipping them here would"
              + " report a pass that tested no SQL.");
    }
    Assumptions.abort(DB_URL_ENV + " is not set; skipping the suites that need a database");
    throw new AssertionError("unreachable");
  }

  /** pgjdbc URL for the given libpq URL, scoped to {@code schema} when non-null. */
  public static String jdbcUrl(String rawUrl, String schema) {
    // One suite's copy tolerated an already-jdbc-prefixed value; keep that tolerance.
    URI uri = URI.create(rawUrl.startsWith("jdbc:") ? rawUrl.substring("jdbc:".length()) : rawUrl);
    List<String> params = new ArrayList<>();
    String userInfo = uri.getUserInfo();
    if (userInfo != null) {
      int colon = userInfo.indexOf(':');
      String user = colon < 0 ? userInfo : userInfo.substring(0, colon);
      params.add("user=" + encode(user));
      if (colon >= 0) {
        params.add("password=" + encode(userInfo.substring(colon + 1)));
      }
    }
    if (schema != null) {
      params.add("currentSchema=" + encode(schema));
    }
    int port = uri.getPort() < 0 ? 5432 : uri.getPort();
    return "jdbc:postgresql://"
        + uri.getHost()
        + ":"
        + port
        + uri.getPath()
        + (params.isEmpty() ? "" : "?" + String.join("&", params));
  }

  private static String encode(String value) {
    return URLEncoder.encode(value, StandardCharsets.UTF_8);
  }
}
