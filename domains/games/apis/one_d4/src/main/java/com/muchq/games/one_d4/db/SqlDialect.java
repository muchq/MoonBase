package com.muchq.games.one_d4.db;

import java.sql.SQLException;
import java.sql.Statement;

/**
 * SQL that differs across engines. Production ships {@link PostgresSqlDialect} only. The H2 variant
 * lives under {@code src/test} and is wired by tests — never by {@code IndexerModule}.
 */
public interface SqlDialect {

  /** Upsert into {@code game_features} keyed on {@code game_url}. */
  String insertGameFeature();

  /**
   * Upsert into {@code indexed_periods} keyed on (player, platform, year_month, exclude_bullet).
   */
  String upsertIndexedPeriod();

  /** Create indexing_requests, game_features, and indexed_periods. */
  void createCoreTables(Statement stmt) throws SQLException;

  /**
   * After {@code exclude_bullet} is added to indexed_periods: drop the old 3-column unique when
   * this engine needs that, then add the 4-column unique.
   */
  void migrateIndexedPeriodsUnique(Statement stmt) throws SQLException;

  void addDedupeKeyUnique(Statement stmt) throws SQLException;

  void createGameFeatureUsernameIndexes(Statement stmt) throws SQLException;

  void createClaimableRequestsIndex(Statement stmt) throws SQLException;
}
