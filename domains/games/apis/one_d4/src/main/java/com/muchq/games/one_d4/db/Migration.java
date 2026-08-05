package com.muchq.games.one_d4.db;

import java.sql.Connection;
import java.sql.SQLException;
import java.sql.Statement;
import javax.sql.DataSource;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

public class Migration {
  private static final Logger LOG = LoggerFactory.getLogger(Migration.class);

  private static final String H2_INDEXING_REQUESTS =
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

  private static final String H2_GAME_FEATURES =
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

  private static final String H2_INDEXED_PERIODS =
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

  private static final String PG_INDEXING_REQUESTS =
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

  private static final String PG_GAME_FEATURES =
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

  private static final String PG_INDEXED_PERIODS =
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

  private final DataSource dataSource;
  private final boolean useH2;

  public Migration(DataSource dataSource, boolean useH2) {
    this.dataSource = dataSource;
    this.useH2 = useH2;
  }

  private static final String ADD_EXCLUDE_BULLET_COLUMN =
      "ALTER TABLE indexing_requests ADD COLUMN IF NOT EXISTS exclude_bullet BOOLEAN NOT NULL"
          + " DEFAULT FALSE";

  // Add exclude_bullet to indexed_periods and update the unique constraint so the period cache
  // is keyed by (player, platform, year_month, exclude_bullet). Existing rows get false (bullet
  // games included), matching pre-existing behavior. On PG we must drop the old 3-column
  // constraint before adding the 4-column one; H2 supports ADD CONSTRAINT IF NOT EXISTS but
  // PG does not, so PG uses a DO block with EXCEPTION handling instead.
  private static final String ADD_INDEXED_PERIODS_EXCLUDE_BULLET_COLUMN =
      "ALTER TABLE indexed_periods ADD COLUMN IF NOT EXISTS exclude_bullet BOOLEAN NOT NULL"
          + " DEFAULT FALSE";
  private static final String DROP_INDEXED_PERIODS_OLD_UNIQUE_PG =
      "ALTER TABLE indexed_periods DROP CONSTRAINT IF EXISTS"
          + " indexed_periods_player_platform_year_month_key";
  private static final String ADD_INDEXED_PERIODS_UNIQUE_H2 =
      "ALTER TABLE indexed_periods ADD CONSTRAINT IF NOT EXISTS indexed_periods_unique"
          + " UNIQUE (player, platform, year_month, exclude_bullet)";
  private static final String ADD_INDEXED_PERIODS_UNIQUE_PG =
      """
      DO $$ BEGIN
        ALTER TABLE indexed_periods ADD CONSTRAINT indexed_periods_unique
          UNIQUE (player, platform, year_month, exclude_bullet);
      EXCEPTION WHEN duplicate_table OR duplicate_object THEN NULL;
      END $$\
      """;

  private static final String ADD_INDEXED_AT_COLUMN =
      "ALTER TABLE game_features ADD COLUMN IF NOT EXISTS indexed_at TIMESTAMP NOT NULL DEFAULT"
          + " now()";
  private static final String DROP_MOTIFS_JSON_COLUMN_H2 =
      "ALTER TABLE game_features DROP COLUMN IF EXISTS motifs_json";
  private static final String DROP_MOTIFS_JSON_COLUMN_PG =
      "ALTER TABLE game_features DROP COLUMN IF EXISTS motifs_json";

  // Drop has_* boolean motif columns — queries now use motif_occurrences directly.
  // Issued one per statement because H2 doesn't support comma-separated multi-column drops.
  private static final String[] DROP_HAS_MOTIF_COLUMNS = {
    "ALTER TABLE game_features DROP COLUMN IF EXISTS has_pin",
    "ALTER TABLE game_features DROP COLUMN IF EXISTS has_cross_pin",
    "ALTER TABLE game_features DROP COLUMN IF EXISTS has_fork",
    "ALTER TABLE game_features DROP COLUMN IF EXISTS has_skewer",
    "ALTER TABLE game_features DROP COLUMN IF EXISTS has_discovered_attack",
    "ALTER TABLE game_features DROP COLUMN IF EXISTS has_discovered_mate",
    "ALTER TABLE game_features DROP COLUMN IF EXISTS has_discovered_check",
    "ALTER TABLE game_features DROP COLUMN IF EXISTS has_check",
    "ALTER TABLE game_features DROP COLUMN IF EXISTS has_checkmate",
    "ALTER TABLE game_features DROP COLUMN IF EXISTS has_promotion",
    "ALTER TABLE game_features DROP COLUMN IF EXISTS has_promotion_with_check",
    "ALTER TABLE game_features DROP COLUMN IF EXISTS has_promotion_with_checkmate",
    "ALTER TABLE game_features DROP COLUMN IF EXISTS has_back_rank_mate",
    "ALTER TABLE game_features DROP COLUMN IF EXISTS has_smothered_mate",
    "ALTER TABLE game_features DROP COLUMN IF EXISTS has_sacrifice",
    "ALTER TABLE game_features DROP COLUMN IF EXISTS has_zugzwang",
    "ALTER TABLE game_features DROP COLUMN IF EXISTS has_double_check",
    "ALTER TABLE game_features DROP COLUMN IF EXISTS has_interference",
    "ALTER TABLE game_features DROP COLUMN IF EXISTS has_overloaded_piece",
  };

  // motif_occurrences: one row per motif firing per game. Dialect-neutral (UUID stored as string).
  private static final String CREATE_MOTIF_OCCURRENCES =
      """
      CREATE TABLE IF NOT EXISTS motif_occurrences (
          id           VARCHAR(36) NOT NULL PRIMARY KEY,
          game_url     VARCHAR(1024) NOT NULL REFERENCES game_features(game_url) ON DELETE CASCADE,
          motif        VARCHAR(50) NOT NULL,
          ply          INT NOT NULL,
          side         VARCHAR(5) NOT NULL,
          move_number  INT NOT NULL,
          description  TEXT,
          moved_piece  VARCHAR(20),
          attacker     VARCHAR(20),
          target       VARCHAR(20),
          is_discovered BOOLEAN NOT NULL DEFAULT FALSE,
          is_mate       BOOLEAN NOT NULL DEFAULT FALSE
      )
      """;
  private static final String CREATE_IDX_MOTIF_OCC_GAME_URL =
      "CREATE INDEX IF NOT EXISTS idx_motif_occ_game_url ON motif_occurrences(game_url)";
  private static final String CREATE_IDX_MOTIF_OCC_MOTIF =
      "CREATE INDEX IF NOT EXISTS idx_motif_occ_motif ON motif_occurrences(motif)";
  private static final String CREATE_IDX_MOTIF_OCC_PLY =
      "CREATE INDEX IF NOT EXISTS idx_motif_occ_ply ON motif_occurrences(game_url, ply)";

  // Structured fields for discovered attack/check occurrences (legacy ALTER TABLE)
  private static final String ADD_OCC_MOVED_PIECE =
      "ALTER TABLE motif_occurrences ADD COLUMN IF NOT EXISTS moved_piece VARCHAR(20)";
  private static final String ADD_OCC_ATTACKER =
      "ALTER TABLE motif_occurrences ADD COLUMN IF NOT EXISTS attacker VARCHAR(20)";
  private static final String ADD_OCC_TARGET =
      "ALTER TABLE motif_occurrences ADD COLUMN IF NOT EXISTS target VARCHAR(20)";
  private static final String ADD_OCC_IS_DISCOVERED =
      "ALTER TABLE motif_occurrences ADD COLUMN IF NOT EXISTS is_discovered BOOLEAN NOT NULL"
          + " DEFAULT FALSE";
  private static final String ADD_OCC_IS_MATE =
      "ALTER TABLE motif_occurrences ADD COLUMN IF NOT EXISTS is_mate BOOLEAN NOT NULL DEFAULT"
          + " FALSE";
  private static final String ADD_OCC_PIN_TYPE =
      "ALTER TABLE motif_occurrences ADD COLUMN IF NOT EXISTS pin_type VARCHAR(8)";

  // dedupe_key enforces "at most one live request per (player, platform, start_month, end_month,
  // exclude_bullet)" in the database, closing the check-then-act race in IndexRequestService
  // (#1249). It carries the composite key while a request is PENDING/PROCESSING and NULL once it
  // reaches a terminal status — a plain UNIQUE constraint ignores NULLs on both engines, so
  // terminal rows accumulate freely while live ones cannot collide.
  //
  // A Postgres partial unique index would express this more directly, but H2 does not support
  // one, and H2 is what the default CI suite runs — the pg-backed suites skip silently without
  // GOLF_HUB_TEST_DB_URL/PG_TEST_DB_URL. A PG-only constraint would leave the race guard
  // effectively untested on every ordinary PR. One nullable column costs a little schema surface
  // and buys the same invariant, enforced identically on both engines and exercised by db_tests.
  private static final String ADD_DEDUPE_KEY_COLUMN =
      "ALTER TABLE indexing_requests ADD COLUMN IF NOT EXISTS dedupe_key VARCHAR(600)";

  // The (created_at, id) order here is the same one findExistingRequest uses, and they have to stay
  // that way: this picks which duplicate holds the slot, that picks which one a submit attaches to,
  // and if they disagree a caller can be handed a row nobody is working on while the keyed row does
  // the work. created_at alone does not settle it — ties are exactly what duplicate submits
  // produce.
  //
  // Backfill before the constraint exists, and only onto the row findExistingRequest would already
  // have returned (oldest non-terminal per group). The pre-constraint schema permitted duplicate
  // live rows, so any group holding several would fail the ADD CONSTRAINT below if they were all
  // keyed. Losers keep dedupe_key NULL: they stay visible and keep their status, they simply hold
  // no slot, and the staleness reclamation retires them on its own clock.
  // Keys exactly one live row per group, in a single statement.
  //
  // Two details, both learned by watching this fail:
  //
  // LOWER(CAST(... AS VARCHAR)) rather than bare concatenation, because H2 renders a BOOLEAN as
  // 'TRUE' while Postgres and Boolean.toString render 'true'. Left implicit, a backfilled row's
  // key would not match the one IndexingRequestDao computes in Java on H2, and dedupe would miss
  // it exactly once.
  //
  // And the winner is picked by a total order — (created_at, id), with ids compared by '<' rather
  // than MIN(id), since Postgres has ordering operators for uuid but no min/max aggregate over it.
  // Selecting on MIN(created_at) alone is not enough: duplicate submits are precisely the rows
  // most likely to share a timestamp, a tie makes MIN match every row in the group, and the
  // statement then tries to write one key onto several rows. Cleaning that up afterwards is too
  // late — on an upgrade where the constraint already exists the UPDATE itself is rejected, and on
  // a first run ADD CONSTRAINT is. Either way the migration aborts, so the tiebreak has to be part
  // of the selection rather than a follow-up pass.
  //
  // The IS NULL guards make re-running the migration a no-op.
  private static final String BACKFILL_DEDUPE_KEY =
      """
      UPDATE indexing_requests r
      SET dedupe_key = r.platform || '|' || r.start_month || '|' || r.end_month || '|'
                       || LOWER(CAST(r.exclude_bullet AS VARCHAR)) || '|' || r.player
      WHERE r.status IN ('PENDING', 'PROCESSING')
        AND r.dedupe_key IS NULL
        AND NOT EXISTS (
          SELECT 1 FROM indexing_requests h
          WHERE h.player = r.player AND h.platform = r.platform
            AND h.start_month = r.start_month AND h.end_month = r.end_month
            AND h.exclude_bullet = r.exclude_bullet
            AND h.status IN ('PENDING', 'PROCESSING')
            AND h.dedupe_key IS NOT NULL)
        AND NOT EXISTS (
          SELECT 1 FROM indexing_requests o
          WHERE o.player = r.player AND o.platform = r.platform
            AND o.start_month = r.start_month AND o.end_month = r.end_month
            AND o.exclude_bullet = r.exclude_bullet
            AND o.status IN ('PENDING', 'PROCESSING')
            AND o.dedupe_key IS NULL
            AND (o.created_at < r.created_at
                 OR (o.created_at = r.created_at AND o.id < r.id)))
      """;

  // Explicit ownership, replacing the inference that a request whose updated_at has not moved must
  // be abandoned. owner_id names the worker that holds the request; lease_expires_at is how long
  // that claim is good for without renewal. Both NULL means nobody has claimed it yet — which is a
  // different state from "claimed and gone quiet", and the sweep now has to tell them apart.
  //
  // Nullable with no backfill on purpose. Rows already in flight when this deploys have no owner,
  // so they read as unclaimed and fall to the orphan clock, which is exactly how they were being
  // handled before this column existed. Anything else would mean inventing an owner for a worker
  // this process cannot see.
  private static final String[] ADD_LEASE_COLUMNS = {
    "ALTER TABLE indexing_requests ADD COLUMN IF NOT EXISTS owner_id VARCHAR(128)",
    "ALTER TABLE indexing_requests ADD COLUMN IF NOT EXISTS lease_expires_at TIMESTAMP",
  };

  // The reclaim sweep looks for live rows whose lease has run out, and the orphan sweep for live
  // rows that never had one. Both filter on status plus a lease column every hour.
  private static final String CREATE_IDX_REQUESTS_LEASE =
      "CREATE INDEX IF NOT EXISTS idx_indexing_requests_lease"
          + " ON indexing_requests(status, lease_expires_at)";

  /**
   * Columns that make the table dispatchable (#1279).
   *
   * <p>{@code skip_cache} has to be persisted because it stops being the submitter's business the
   * moment any worker can pick the row up. Every other field a run needs is already a column; this
   * one lived only in the queue message, so a worker claiming from the table would silently honour
   * the period cache for a request that asked to bypass it.
   *
   * <p>{@code attempts} is the bound that replaces "nobody ever picked this up". Once an expired
   * lease returns work to the queue instead of retiring it, a request that reliably kills its
   * worker is retried forever, across every instance — a possibility that did not exist while a
   * crashed process took its queue with it. Incremented on each claim, so it counts attempts and
   * not failures, which is the conservative direction: a worker killed before it could report
   * anything still moves the counter.
   *
   * <p>Both default for existing rows, and both defaults are the pre-#1279 behaviour: requests in
   * flight during a deploy did not skip the cache and have not been attempted by a table-claiming
   * worker.
   *
   * <p>The DEFAULT is the whole backfill. Both engines fill existing rows from it as the column is
   * added, so there is no second statement to write — and an earlier version of this that added one
   * anyway was dead code that no test could distinguish from the DEFAULT doing its job. Both
   * columns are read as primitives ({@code getBoolean}, {@code getInt}), so a NULL would silently
   * arrive as false or zero rather than failing; the DEFAULT is what keeps that from being
   * load-bearing.
   */
  private static final String[] ADD_DISPATCH_COLUMNS = {
    // The DEFAULTs are what backfills existing rows — both engines apply them when the column is
    // added, so a request in flight during a deploy comes out as "do not skip the cache, never
    // attempted", which is exactly its pre-#1279 behaviour. Both are read as primitives, so a NULL
    // here would arrive silently as the same values without anyone having decided that.
    "ALTER TABLE indexing_requests ADD COLUMN IF NOT EXISTS skip_cache BOOLEAN DEFAULT FALSE",
    "ALTER TABLE indexing_requests ADD COLUMN IF NOT EXISTS attempts INT DEFAULT 0",
  };

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
  private static final String CREATE_IDX_REQUESTS_CLAIMABLE_PG =
      "CREATE INDEX IF NOT EXISTS idx_indexing_requests_claimable"
          + " ON indexing_requests(created_at) WHERE status IN ('PENDING', 'PROCESSING')";

  /**
   * The same index for H2, which has no partial indexes. The composite is what a partial index
   * approximates there, and H2 is the test engine rather than the deployment target, so the cost of
   * it being the weaker plan is a slower test rather than a slower production poll.
   */
  private static final String CREATE_IDX_REQUESTS_CLAIMABLE_H2 =
      "CREATE INDEX IF NOT EXISTS idx_indexing_requests_claimable"
          + " ON indexing_requests(status, created_at)";

  // The retention sweep's anti-join (deleteOlderThan) filters indexing_requests on an hourly
  // schedule by "does any game still point at me". Without this, EXPLAIN shows a hash anti-join
  // over a sequential scan of game_features — the largest table in the schema. Neither engine
  // indexes a foreign key column automatically.
  private static final String CREATE_IDX_GAME_FEATURES_REQUEST_ID =
      "CREATE INDEX IF NOT EXISTS idx_game_features_request_id ON game_features(request_id)";

  // The browse ordering. Every loosely-filtered /v1/query — including the first-load default that
  // FirstPageCache warms and the page-2 prefetch the cache deliberately excludes — ends in
  // SqlCompiler's ORDER BY g.played_at DESC, g.game_url ASC LIMIT n. Without an index in that
  // exact column order and direction the plan is a full scan plus top-N sort of the whole table
  // per page; with it, a LIMIT-sized index walk. The columns and directions here must stay in
  // step with SqlCompiler's ORDER BY or the index stops satisfying the sort.
  private static final String CREATE_IDX_GAME_FEATURES_PLAYED_AT =
      "CREATE INDEX IF NOT EXISTS idx_game_features_played_at"
          + " ON game_features(played_at DESC, game_url ASC)";

  private static final String ADD_DEDUPE_KEY_UNIQUE_H2 =
      "ALTER TABLE indexing_requests ADD CONSTRAINT IF NOT EXISTS indexing_requests_dedupe_unique"
          + " UNIQUE (dedupe_key)";
  private static final String ADD_DEDUPE_KEY_UNIQUE_PG =
      """
      DO $$ BEGIN
        ALTER TABLE indexing_requests ADD CONSTRAINT indexing_requests_dedupe_unique
          UNIQUE (dedupe_key);
      EXCEPTION WHEN duplicate_table OR duplicate_object THEN NULL;
      END $$\
      """;

  // Opening name/family (derived from the chess.com ECOUrl) and player titles. Existing rows get
  // NULL until the affected periods are reindexed.
  private static final String[] ADD_OPENING_AND_TITLE_COLUMNS = {
    "ALTER TABLE game_features ADD COLUMN IF NOT EXISTS white_title VARCHAR(10)",
    "ALTER TABLE game_features ADD COLUMN IF NOT EXISTS black_title VARCHAR(10)",
    "ALTER TABLE game_features ADD COLUMN IF NOT EXISTS opening_name VARCHAR(255)",
    "ALTER TABLE game_features ADD COLUMN IF NOT EXISTS opening_family VARCHAR(255)",
  };

  public void run() {
    try (Connection conn = dataSource.getConnection();
        Statement stmt = conn.createStatement()) {

      if (useH2) {
        stmt.execute(H2_INDEXING_REQUESTS);
        stmt.execute(H2_GAME_FEATURES);
        stmt.execute(H2_INDEXED_PERIODS);
      } else {
        stmt.execute(PG_INDEXING_REQUESTS);
        stmt.execute(PG_GAME_FEATURES);
        stmt.execute(PG_INDEXED_PERIODS);
      }

      stmt.execute(ADD_EXCLUDE_BULLET_COLUMN);
      stmt.execute(ADD_INDEXED_PERIODS_EXCLUDE_BULLET_COLUMN);
      if (!useH2) {
        stmt.execute(DROP_INDEXED_PERIODS_OLD_UNIQUE_PG);
      }
      stmt.execute(useH2 ? ADD_INDEXED_PERIODS_UNIQUE_H2 : ADD_INDEXED_PERIODS_UNIQUE_PG);
      stmt.execute(ADD_INDEXED_AT_COLUMN);

      // Drop legacy motifs_json column (replaced by motif_occurrences table)
      if (useH2) {
        stmt.execute(DROP_MOTIFS_JSON_COLUMN_H2);
      } else {
        stmt.execute(DROP_MOTIFS_JSON_COLUMN_PG);
      }

      stmt.execute(CREATE_MOTIF_OCCURRENCES);
      stmt.execute(CREATE_IDX_MOTIF_OCC_GAME_URL);
      stmt.execute(CREATE_IDX_MOTIF_OCC_MOTIF);
      stmt.execute(CREATE_IDX_MOTIF_OCC_PLY);

      // Structured fields on motif_occurrences
      stmt.execute(ADD_OCC_MOVED_PIECE);
      stmt.execute(ADD_OCC_ATTACKER);
      stmt.execute(ADD_OCC_TARGET);
      stmt.execute(ADD_OCC_IS_DISCOVERED);
      stmt.execute(ADD_OCC_IS_MATE);

      // Pin type for motif_occurrences
      stmt.execute(ADD_OCC_PIN_TYPE);

      // Opening name/family and player title columns
      for (String add : ADD_OPENING_AND_TITLE_COLUMNS) {
        stmt.execute(add);
      }

      // Drop has_* boolean motif columns — queries now target motif_occurrences directly.
      for (String drop : DROP_HAS_MOTIF_COLUMNS) {
        stmt.execute(drop);
      }

      // Live-request dedupe key. Strictly ordered: the column has to exist before the backfill can
      // write it, and the data has to be unique before the constraint can be added.
      stmt.execute(ADD_DEDUPE_KEY_COLUMN);
      stmt.execute(BACKFILL_DEDUPE_KEY);
      stmt.execute(useH2 ? ADD_DEDUPE_KEY_UNIQUE_H2 : ADD_DEDUPE_KEY_UNIQUE_PG);
      stmt.execute(CREATE_IDX_GAME_FEATURES_REQUEST_ID);
      stmt.execute(CREATE_IDX_GAME_FEATURES_PLAYED_AT);

      // Ownership leases.
      for (String add : ADD_LEASE_COLUMNS) {
        stmt.execute(add);
      }
      stmt.execute(CREATE_IDX_REQUESTS_LEASE);

      // Table-backed dispatch.
      for (String add : ADD_DISPATCH_COLUMNS) {
        stmt.execute(add);
      }
      stmt.execute(useH2 ? CREATE_IDX_REQUESTS_CLAIMABLE_H2 : CREATE_IDX_REQUESTS_CLAIMABLE_PG);

      LOG.info("Database migration completed successfully (H2={})", useH2);
    } catch (SQLException e) {
      throw new RuntimeException("Failed to run database migration", e);
    }
  }
}
