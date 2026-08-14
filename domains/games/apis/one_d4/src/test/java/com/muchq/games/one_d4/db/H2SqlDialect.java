package com.muchq.games.one_d4.db;

import java.sql.SQLException;
import java.sql.Statement;

/**
 * H2 dialect for the test suite. Production never sees this class — it is under {@code src/test}
 * and wired by {@code TestDb} / {@code H2TestSqlDialectFactory}, not by {@code IndexerModule}.
 */
public final class H2SqlDialect implements SqlDialect {

  public static final H2SqlDialect INSTANCE = new H2SqlDialect();

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

  private static final String INDEXING_REQUESTS =
      """
      CREATE TABLE IF NOT EXISTS indexing_requests (
          id             UUID DEFAULT random_uuid() PRIMARY KEY,
          player         VARCHAR(255) NOT NULL,
          platform       VARCHAR(50) NOT NULL,
          start_month    VARCHAR(7) NOT NULL,
          end_month      VARCHAR(7) NOT NULL,
          status         VARCHAR(20) NOT NULL DEFAULT 'PENDING',
          created_at     TIMESTAMP NOT NULL DEFAULT current_timestamp(),
          updated_at     TIMESTAMP NOT NULL DEFAULT current_timestamp(),
          error_message  TEXT,
          games_indexed  INT DEFAULT 0,
          exclude_bullet BOOLEAN NOT NULL DEFAULT FALSE
      )
      """;

  private static final String GAME_FEATURES =
      """
      CREATE TABLE IF NOT EXISTS game_features (
          id            UUID DEFAULT random_uuid() PRIMARY KEY,
          request_id    UUID NOT NULL REFERENCES indexing_requests(id),
          game_url      VARCHAR(1024) NOT NULL UNIQUE,
          platform      VARCHAR(50) NOT NULL,
          white_username VARCHAR(255),
          black_username VARCHAR(255),
          white_elo     INT,
          black_elo     INT,
          white_title   VARCHAR(10),
          black_title   VARCHAR(10),
          time_class    VARCHAR(50),
          eco           VARCHAR(10),
          opening_name  VARCHAR(255),
          opening_family VARCHAR(255),
          result        VARCHAR(20),
          played_at     TIMESTAMP,
          num_moves     INT,
          indexed_at    TIMESTAMP NOT NULL DEFAULT current_timestamp(),
          pgn           TEXT
      )
      """;

  private static final String INDEXED_PERIODS =
      """
      CREATE TABLE IF NOT EXISTS indexed_periods (
          id             UUID DEFAULT random_uuid() PRIMARY KEY,
          player         VARCHAR(255) NOT NULL,
          platform       VARCHAR(50) NOT NULL,
          year_month     VARCHAR(7) NOT NULL,
          fetched_at     TIMESTAMP NOT NULL,
          is_complete    BOOLEAN NOT NULL,
          games_count    INT NOT NULL,
          exclude_bullet BOOLEAN NOT NULL DEFAULT FALSE,
          CONSTRAINT indexed_periods_unique UNIQUE (player, platform, year_month, exclude_bullet)
      )
      """;

  private static final String ADD_INDEXED_PERIODS_UNIQUE =
      "ALTER TABLE indexed_periods ADD CONSTRAINT IF NOT EXISTS indexed_periods_unique"
          + " UNIQUE (player, platform, year_month, exclude_bullet)";

  private static final String ADD_DEDUPE_KEY_UNIQUE =
      "ALTER TABLE indexing_requests ADD CONSTRAINT IF NOT EXISTS indexing_requests_dedupe_unique"
          + " UNIQUE (dedupe_key)";

  /** Plain column indexes: H2 has no expression indexes to serve {@code LOWER(...)}. */
  private static final String[] USERNAME_INDEXES = {
    "CREATE INDEX IF NOT EXISTS idx_game_features_white_username"
        + " ON game_features(white_username)",
    "CREATE INDEX IF NOT EXISTS idx_game_features_black_username"
        + " ON game_features(black_username)",
  };

  /** Composite approximates the Postgres partial index; H2 has no partial indexes. */
  private static final String CLAIMABLE_INDEX =
      "CREATE INDEX IF NOT EXISTS idx_indexing_requests_claimable"
          + " ON indexing_requests(status, created_at)";

  private H2SqlDialect() {}

  @Override
  public String insertGameFeature() {
    return INSERT_GAME_FEATURE;
  }

  @Override
  public String upsertIndexedPeriod() {
    return UPSERT_INDEXED_PERIOD;
  }

  @Override
  public void createCoreTables(Statement stmt) throws SQLException {
    stmt.execute(INDEXING_REQUESTS);
    stmt.execute(GAME_FEATURES);
    stmt.execute(INDEXED_PERIODS);
  }

  @Override
  public void migrateIndexedPeriodsUnique(Statement stmt) throws SQLException {
    stmt.execute(ADD_INDEXED_PERIODS_UNIQUE);
  }

  @Override
  public void addDedupeKeyUnique(Statement stmt) throws SQLException {
    stmt.execute(ADD_DEDUPE_KEY_UNIQUE);
  }

  @Override
  public void createGameFeatureUsernameIndexes(Statement stmt) throws SQLException {
    for (String create : USERNAME_INDEXES) {
      stmt.execute(create);
    }
  }

  @Override
  public void createClaimableRequestsIndex(Statement stmt) throws SQLException {
    stmt.execute(CLAIMABLE_INDEX);
  }
}
