package com.muchq.games.one_d4.db;

import java.sql.Timestamp;
import java.time.Instant;
import java.util.Collection;
import java.util.List;
import java.util.Optional;
import org.jdbi.v3.core.Jdbi;
import org.jdbi.v3.core.mapper.RowMapper;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

public class IndexedPeriodDao implements IndexedPeriodStore {
  private static final Logger LOG = LoggerFactory.getLogger(IndexedPeriodDao.class);

  private static final RowMapper<IndexedPeriod> ROW_MAPPER =
      (rs, ctx) ->
          new IndexedPeriod(
              rs.getString("player"),
              rs.getString("platform"),
              rs.getString("year_month"),
              rs.getTimestamp("fetched_at").toInstant(),
              rs.getBoolean("is_complete"),
              rs.getInt("games_count"),
              rs.getBoolean("exclude_bullet"));

  private static final String FIND_COMPLETE =
      """
      SELECT player, platform, year_month, fetched_at, is_complete, games_count, exclude_bullet
      FROM indexed_periods
      WHERE player = ? AND platform = ? AND year_month = ? AND exclude_bullet = ?
        AND is_complete = true
      """;

  private static final String FIND_FOR_PLAYERS =
      """
      SELECT player, platform, year_month, fetched_at, is_complete, games_count, exclude_bullet
      FROM indexed_periods
      WHERE player IN (<players>)
      """;

  private static final String DELETE_OLDER_THAN =
      "DELETE FROM indexed_periods WHERE fetched_at < ?";

  private final Jdbi jdbi;
  private final SqlDialect dialect;

  public IndexedPeriodDao(Jdbi jdbi, SqlDialect dialect) {
    this.jdbi = jdbi;
    this.dialect = dialect;
  }

  @Override
  public Optional<IndexedPeriod> findCompletePeriod(
      String player, String platform, String month, boolean excludeBullet) {
    return jdbi.withHandle(
        h ->
            h.createQuery(FIND_COMPLETE)
                .bind(0, player)
                .bind(1, platform)
                .bind(2, month)
                .bind(3, excludeBullet)
                .map(ROW_MAPPER)
                .findFirst());
  }

  @Override
  public void upsertPeriod(
      String player,
      String platform,
      String month,
      Instant fetchedAt,
      boolean isComplete,
      int gamesCount,
      boolean excludeBullet) {
    String sql = dialect.upsertIndexedPeriod();
    jdbi.useHandle(
        h ->
            h.createUpdate(sql)
                .bind(0, player)
                .bind(1, platform)
                .bind(2, month)
                .bind(3, Timestamp.from(fetchedAt))
                .bind(4, isComplete)
                .bind(5, gamesCount)
                .bind(6, excludeBullet)
                .execute());
  }

  @Override
  public List<IndexedPeriod> findPeriodsForPlayers(Collection<String> players) {
    if (players.isEmpty()) return List.of();
    // Bounded like every serving read: this feeds the data-availability block of GET /v1/index.
    return StatementTimeouts.withStatementTimeout(
        jdbi,
        StatementTimeouts.SERVING_READ_SECONDS,
        h ->
            h.createQuery(FIND_FOR_PLAYERS)
                .bindList("players", List.copyOf(players))
                .map(ROW_MAPPER)
                .list());
  }

  @Override
  public int deleteOlderThan(Instant threshold) {
    // Bounded at the sweep timeout: idempotent hourly cleanup, re-run in an hour if truncated.
    return StatementTimeouts.withStatementTimeout(
        jdbi,
        StatementTimeouts.RETENTION_SWEEP_SECONDS,
        h -> {
          int deleted =
              h.createUpdate(DELETE_OLDER_THAN).bind(0, Timestamp.from(threshold)).execute();
          if (deleted > 0) {
            LOG.debug("Deleted {} indexed periods older than {}", deleted, threshold);
          }
          return deleted;
        });
  }
}
