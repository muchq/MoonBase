package com.muchq.games.one_d4.db;

import java.sql.SQLException;
import java.sql.Statement;

/** The only dialect the service ships. H2 lives under {@code src/test}. */
public final class PostgresSqlDialect implements SqlDialect {

  public static final PostgresSqlDialect INSTANCE = new PostgresSqlDialect();

  private static final String INSERT_GAME_FEATURE =
      """
      INSERT INTO game_features (
          request_id, game_url, platform, white_username, black_username,
          white_elo, black_elo, white_title, black_title, time_class, eco,
          opening_name, opening_family, result, played_at, num_moves,
          indexed_at, pgn
      ) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)
      ON CONFLICT (game_url) DO UPDATE SET
          indexed_at = EXCLUDED.indexed_at,
          request_id = EXCLUDED.request_id,
          white_title = EXCLUDED.white_title,
          black_title = EXCLUDED.black_title,
          opening_name = EXCLUDED.opening_name,
          opening_family = EXCLUDED.opening_family
      """;

  private static final String UPSERT_INDEXED_PERIOD =
      """
      INSERT INTO indexed_periods
          (player, platform, year_month, fetched_at, is_complete, games_count, exclude_bullet)
      VALUES (?, ?, ?, ?, ?, ?, ?)
      ON CONFLICT (player, platform, year_month, exclude_bullet)
      DO UPDATE SET fetched_at = EXCLUDED.fetched_at, is_complete = EXCLUDED.is_complete,
                    games_count = EXCLUDED.games_count
      """;

  private static final String INDEXING_REQUESTS =
      """
      CREATE TABLE IF NOT EXISTS indexing_requests (
          id             UUID PRIMARY KEY DEFAULT gen_random_uuid(),
          player         VARCHAR(255) NOT NULL,
          platform       VARCHAR(50) NOT NULL,
          start_month    VARCHAR(7) NOT NULL,
          end_month      VARCHAR(7) NOT NULL,
          status         VARCHAR(20) NOT NULL DEFAULT 'PENDING',
          created_at     TIMESTAMP NOT NULL DEFAULT now(),
          updated_at     TIMESTAMP NOT NULL DEFAULT now(),
          error_message  TEXT,
          games_indexed  INT DEFAULT 0,
          exclude_bullet BOOLEAN NOT NULL DEFAULT FALSE
      )
      """;

  private static final String GAME_FEATURES =
      """
      CREATE TABLE IF NOT EXISTS game_features (
          id            UUID PRIMARY KEY DEFAULT gen_random_uuid(),
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
          indexed_at    TIMESTAMP NOT NULL DEFAULT now(),
          pgn           TEXT
      )
      """;

  private static final String INDEXED_PERIODS =
      """
      CREATE TABLE IF NOT EXISTS indexed_periods (
          id             UUID PRIMARY KEY DEFAULT gen_random_uuid(),
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

  private static final String DROP_INDEXED_PERIODS_OLD_UNIQUE =
      "ALTER TABLE indexed_periods DROP CONSTRAINT IF EXISTS"
          + " indexed_periods_player_platform_year_month_key";

  private static final String ADD_INDEXED_PERIODS_UNIQUE =
      """
      DO $$ BEGIN
        ALTER TABLE indexed_periods ADD CONSTRAINT indexed_periods_unique
          UNIQUE (player, platform, year_month, exclude_bullet);
      EXCEPTION WHEN duplicate_table OR duplicate_object THEN NULL;
      END $$\
      """;

  private static final String ADD_DEDUPE_KEY_UNIQUE =
      """
      DO $$ BEGIN
        ALTER TABLE indexing_requests ADD CONSTRAINT indexing_requests_dedupe_unique
          UNIQUE (dedupe_key);
      EXCEPTION WHEN duplicate_table OR duplicate_object THEN NULL;
      END $$\
      """;

  private static final String[] USERNAME_INDEXES = {
    "CREATE INDEX IF NOT EXISTS idx_game_features_white_username"
        + " ON game_features(LOWER(white_username))",
    "CREATE INDEX IF NOT EXISTS idx_game_features_black_username"
        + " ON game_features(LOWER(black_username))",
  };

  /**
   * Partial on Postgres: see Migration's claimable-index comment for why the WHERE clause is not a
   * preference. Measured cheaper than a composite by three orders of magnitude at 200k rows.
   */
  private static final String CLAIMABLE_INDEX =
      "CREATE INDEX IF NOT EXISTS idx_indexing_requests_claimable"
          + " ON indexing_requests(created_at) WHERE status IN ('PENDING', 'PROCESSING')";

  private PostgresSqlDialect() {}

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
    stmt.execute(DROP_INDEXED_PERIODS_OLD_UNIQUE);
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
