package com.muchq.games.one_d4.db;

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
    return "pg";
  }
}
