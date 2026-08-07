package com.muchq.games.one_d4.db;

import java.net.URI;
import java.net.URLEncoder;
import java.nio.charset.StandardCharsets;
import java.util.ArrayList;
import java.util.List;

/**
 * Converts the libpq-style URL CI exports for the PG-gated suites ({@code
 * postgresql://user:pass@host:port/db}) into a pgjdbc URL. pgjdbc does not accept credentials in
 * the authority, so they move to query params; each suite passes its own {@code currentSchema} so
 * suites sharing the scratch database cannot collide. Extracted here once it had been copied into a
 * fourth test class.
 */
public final class PgTestUrls {

  private PgTestUrls() {}

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
