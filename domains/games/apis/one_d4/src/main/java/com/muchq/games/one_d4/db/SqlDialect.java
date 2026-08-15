package com.muchq.games.one_d4.db;

import java.util.List;

/**
 * SQL that differs across engines. Production ships {@link PostgresSqlDialect} only. The H2 variant
 * lives under {@code src/test} and is wired by tests — never by {@code IndexerModule}.
 *
 * <p>Pure SQL: every method returns statements for {@link Migration} (or the DAOs) to execute. The
 * dialect does not take a {@link java.sql.Statement}; the migration's ordering stays in one place.
 */
public interface SqlDialect {

  /** Upsert into {@code game_features} keyed on {@code game_url}. */
  String insertGameFeature();

  /**
   * Upsert into {@code indexed_periods} keyed on (player, platform, year_month, exclude_bullet).
   */
  String upsertIndexedPeriod();

  /**
   * Create {@code indexing_requests}, {@code game_features}, and {@code indexed_periods}, in that
   * order.
   */
  List<String> createCoreTables();

  /**
   * After {@code exclude_bullet} is added to indexed_periods: statements that drop the old 3-column
   * unique when this engine needs that, then add the 4-column unique.
   */
  List<String> indexedPeriodsUniqueMigration();

  /** Add the unique constraint on {@code indexing_requests.dedupe_key}. */
  String addDedupeKeyUnique();

  /**
   * Indexes behind username search. Postgres uses expression indexes on {@code LOWER(...)}; H2 has
   * no expression indexes and carries plain-column stand-ins so the migration path stays identical.
   */
  List<String> usernameIndexes();

  /**
   * Index behind {@code claimNext}. Postgres uses a partial index; H2 approximates with a composite
   * because it has no partial indexes.
   */
  String claimableRequestsIndex();
}
