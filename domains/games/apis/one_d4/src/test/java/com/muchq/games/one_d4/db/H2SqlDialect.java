package com.muchq.games.one_d4.db;

/**
 * H2 dialect for the test suite. Production never sees this class — it is under {@code src/test}
 * and wired by {@code TestDb} / {@code TestSqlDialectFactory}, not by {@code IndexerModule}. Its
 * DDL is the test-only {@code migrations/h2/} files, off the production classpath the same way this
 * class is.
 */
public final class H2SqlDialect implements SqlDialect {

  private static final String INSERT_GAME_FEATURE =
      """
      MERGE INTO game_features (
          request_id, game_url, platform, white_username, black_username,
          white_elo, black_elo, white_title, black_title, time_class, eco,
          opening_name, opening_family, result, played_at, num_moves,
          indexed_at, pgn
      ) KEY (game_url) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)
      """;

  private static final String UPSERT_INDEXED_PERIOD =
      """
      MERGE INTO indexed_periods
          (player, platform, year_month, fetched_at, is_complete, games_count, exclude_bullet)
      KEY (player, platform, year_month, exclude_bullet)
      VALUES (?, ?, ?, ?, ?, ?, ?)
      """;

  @Override
  public String insertGameFeature() {
    return INSERT_GAME_FEATURE;
  }

  @Override
  public String upsertIndexedPeriod() {
    return UPSERT_INDEXED_PERIOD;
  }

  @Override
  public String migrationsEngine() {
    return "h2";
  }

  /** No deploy step runs ahead of an in-memory database, so the H2 path migrates at boot. */
  @Override
  public boolean migratedBeforeBoot() {
    return false;
  }
}
