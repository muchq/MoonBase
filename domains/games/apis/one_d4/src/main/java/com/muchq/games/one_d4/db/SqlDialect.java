package com.muchq.games.one_d4.db;

/**
 * SQL that differs across engines. Production ships {@link PostgresSqlDialect} only. The H2 variant
 * lives under {@code src/test} and is wired by tests — never by {@code IndexerModule}.
 *
 * <p>Only runtime statements for the DAOs live here. The DDL is the {@code migrations/} .sql files
 * (#1419); a dialect's part in those is naming which engine directory {@link Migration} resolves
 * forked steps from.
 */
public interface SqlDialect {

  /** Upsert into {@code game_features} keyed on {@code game_url}. */
  String insertGameFeature();

  /**
   * Upsert into {@code indexed_periods} keyed on (player, platform, year_month, exclude_bullet).
   */
  String upsertIndexedPeriod();

  /**
   * The {@code migrations/} engine directory this dialect's forked DDL lives in: {@code "pg"} or
   * {@code "h2"}. Both engines run the same step list in the same order; only a forked step's SQL
   * differs. See {@code migrations/README.md}.
   */
  String migrationsEngine();

  /**
   * Whether something has already applied the migrations by the time this service boots. Postgres
   * has the {@code one_d4_migrate} one-shot in front of it, so boot runs {@link Migration#verify}
   * and writes no schema; the H2 test path has nothing in front of it and applies them itself.
   *
   * <p>Defaults to true so a dialect added for a deployed engine cannot become a second writer of
   * the schema by omission.
   */
  default boolean migratedBeforeBoot() {
    return true;
  }
}
