package com.muchq.games.one_d4.db;

import java.util.List;

/** The only dialect the service ships. H2 lives under {@code src/test}. */
public final class PostgresSqlDialect implements SqlDialect {

  /**
   * On conflict, refresh the derived/enriched columns too so that reindexing a period backfills
   * titles and opening names on rows indexed before those columns existed.
   */
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

  /**
   * The indexes behind username search (#1313 item 10). Two compiler paths emit the same
   * case-folded predicate shape, {@code LOWER(white_username) = LOWER(?)} OR'd across the sides:
   * the browse UI's username search compiles {@code white.username = "x" OR black.username = "x"}
   * through the STRING_COLUMNS equality branch — the highest-traffic consumer — and every
   * perspective-field query runs the participation guard. Case-folded on <em>both</em> sides, so a
   * plain column index can never serve either on Postgres: these are expression indexes on {@code
   * LOWER(...)}. One per side rather than any composite — an OR across two columns is answered by a
   * BitmapOr of two independent scans, and a BitmapOr's output is unordered, so a composite with
   * {@code played_at} could not skip the sort either; it would only double index weight on the
   * hottest-write table.
   *
   * <p>Without them these predicates were the full-table scan on the player-search path — and with
   * a 5-connection pool, five concurrent player searches held every connection for the full
   * serving-read bound. {@code PostgresPlayerIndexTest} pins the contract on the deployment dialect
   * for both emitting paths: the plan actually reaches these indexes for the compiler's exact
   * predicates, so either side drifting (a compiler path losing its {@code LOWER}, or an index
   * expression changing) fails a test.
   *
   * <p>Ops note: created at boot without {@code CONCURRENTLY}, so the first deploy onto a populated
   * table holds a SHARE lock for the build and pauses the indexer's writes; later boots are IF NOT
   * EXISTS no-ops. ({@code CONCURRENTLY} + {@code IF NOT EXISTS} would be worse — a failed build
   * leaves an INVALID index behind that IF NOT EXISTS then skips forever.)
   */
  private static final List<String> USERNAME_INDEXES =
      List.of(
          "CREATE INDEX IF NOT EXISTS idx_game_features_white_username"
              + " ON game_features(LOWER(white_username))",
          "CREATE INDEX IF NOT EXISTS idx_game_features_black_username"
              + " ON game_features(LOWER(black_username))");

  /**
   * What {@code claimNext} scans: the oldest live row nobody currently holds, on a query every
   * instance runs every few seconds. Ordered by {@code created_at} because the queue it replaces
   * was FIFO, and a poller that skipped ahead would starve the front under sustained load.
   *
   * <p>Partial on Postgres, and that is not a preference. Postgres before 17 cannot emit btree
   * output already ordered on a trailing column when the leading one sits under a {@code
   * ScalarArrayOp}, which {@code status IN (...)} is — so a composite {@code (status, created_at)}
   * is never chosen, and forcing it still produces a full top-N sort of every live row. Measured at
   * 200k rows with 10k live, the partial index planned three orders of magnitude cheaper. Worth
   * pinning explicitly because CI runs {@code postgres:18}, where ordered SAOP scans do exist and
   * the composite would have looked perfectly healthy.
   */
  private static final String CLAIMABLE_INDEX =
      "CREATE INDEX IF NOT EXISTS idx_indexing_requests_claimable"
          + " ON indexing_requests(created_at) WHERE status IN ('PENDING', 'PROCESSING')";

  /**
   * The reanalysis queue. Same claim/lease/fence shape as {@code indexing_requests} and
   * deliberately not the same table — see {@link SqlDialect#createReanalysisRequests}.
   *
   * <p>No claimable index. This table takes one row per reanalysis pass — dozens a year, not the
   * hundreds of thousands {@code indexing_requests} holds — so the partial index that one needs
   * would here cost writes to serve a sequential scan of a table that fits in a page.
   *
   * <p>{@code cursor_game_url} is the keyset cursor: the last {@code game_url} a completed page
   * covered, an exclusive lower bound for the next. Replaces the OFFSET paging both admin passes
   * used, which skipped rows inserted mid-pass and needed a second run to catch them.
   */
  private static final String REANALYSIS_REQUESTS =
      """
      CREATE TABLE IF NOT EXISTS reanalysis_requests (
          id               UUID PRIMARY KEY DEFAULT gen_random_uuid(),
          status           VARCHAR(20) NOT NULL DEFAULT 'PENDING',
          created_at       TIMESTAMP NOT NULL DEFAULT now(),
          updated_at       TIMESTAMP NOT NULL DEFAULT now(),
          owner_id         VARCHAR(128),
          lease_expires_at TIMESTAMP,
          attempts         INT NOT NULL DEFAULT 0,
          error_message    TEXT,
          cursor_game_url  VARCHAR(1024),
          games_processed  INT NOT NULL DEFAULT 0,
          games_failed     INT NOT NULL DEFAULT 0
      )
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
  public String createReanalysisRequests() {
    return REANALYSIS_REQUESTS;
  }

  @Override
  public List<String> createCoreTables() {
    return List.of(INDEXING_REQUESTS, GAME_FEATURES, INDEXED_PERIODS);
  }

  @Override
  public List<String> indexedPeriodsUniqueMigration() {
    return List.of(DROP_INDEXED_PERIODS_OLD_UNIQUE, ADD_INDEXED_PERIODS_UNIQUE);
  }

  @Override
  public String addDedupeKeyUnique() {
    return ADD_DEDUPE_KEY_UNIQUE;
  }

  @Override
  public List<String> usernameIndexes() {
    return USERNAME_INDEXES;
  }

  @Override
  public String claimableRequestsIndex() {
    return CLAIMABLE_INDEX;
  }
}
