package com.muchq.games.one_d4.db;

import java.sql.Connection;
import java.sql.PreparedStatement;
import java.sql.ResultSet;
import java.sql.SQLException;
import java.sql.Timestamp;
import java.time.Instant;
import java.util.Optional;
import javax.sql.DataSource;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

public class IndexedPeriodDao implements IndexedPeriodStore {
  private static final Logger LOG = LoggerFactory.getLogger(IndexedPeriodDao.class);

  private static final String H2_UPSERT =
      """
      MERGE INTO indexed_periods (player, platform, year_month, fetched_at, is_complete, games_count)
      KEY (player, platform, year_month)
      VALUES (?, ?, ?, ?, ?, ?)
      """;

  private static final String PG_UPSERT =
      """
      INSERT INTO indexed_periods (player, platform, year_month, fetched_at, is_complete, games_count)
      VALUES (?, ?, ?, ?, ?, ?)
      ON CONFLICT (player, platform, year_month)
      DO UPDATE SET fetched_at = EXCLUDED.fetched_at, is_complete = EXCLUDED.is_complete,
                    games_count = EXCLUDED.games_count
      """;

  private static final String FIND_COMPLETE =
      """
      SELECT player, platform, year_month, fetched_at, is_complete, games_count
      FROM indexed_periods
      WHERE player = ? AND platform = ? AND year_month = ? AND is_complete = true
      """;

  private static final String DELETE_OLDER_THAN =
      "DELETE FROM indexed_periods WHERE fetched_at < ?";

  private final DataSource dataSource;
  private final boolean useH2;

  public IndexedPeriodDao(DataSource dataSource, boolean useH2) {
    this.dataSource = dataSource;
    this.useH2 = useH2;
  }

  public DataSource getDataSource() {
    return dataSource;
  }

  @Override
  public Optional<IndexedPeriod> findCompletePeriod(String player, String platform, String month) {
    try (Connection conn = dataSource.getConnection();
        PreparedStatement ps = conn.prepareStatement(FIND_COMPLETE)) {
      ps.setString(1, player);
      ps.setString(2, platform);
      ps.setString(3, month);
      try (ResultSet rs = ps.executeQuery()) {
        if (rs.next()) {
          return Optional.of(mapRow(rs));
        }
        return Optional.empty();
      }
    } catch (SQLException e) {
      throw new RuntimeException("Failed to find complete period", e);
    }
  }

  @Override
  public void upsertPeriod(
      String player,
      String platform,
      String month,
      Instant fetchedAt,
      boolean isComplete,
      int gamesCount) {
    String sql = useH2 ? H2_UPSERT : PG_UPSERT;
    try (Connection conn = dataSource.getConnection();
        PreparedStatement ps = conn.prepareStatement(sql)) {
      ps.setString(1, player);
      ps.setString(2, platform);
      ps.setString(3, month);
      ps.setTimestamp(4, Timestamp.from(fetchedAt));
      ps.setBoolean(5, isComplete);
      ps.setInt(6, gamesCount);
      ps.executeUpdate();
    } catch (SQLException e) {
      LOG.error(
          "Failed to upsert indexed period player={} platform={} month={}",
          player,
          platform,
          month,
          e);
      throw new RuntimeException("Failed to upsert indexed period", e);
    }
  }

  @Override
  public void deleteOlderThan(Instant threshold) {
    try (Connection conn = dataSource.getConnection();
        PreparedStatement ps = conn.prepareStatement(DELETE_OLDER_THAN)) {
      ps.setTimestamp(1, Timestamp.from(threshold));
      int deleted = ps.executeUpdate();
      if (deleted > 0) {
        LOG.info("Deleted {} indexed periods older than {}", deleted, threshold);
      }
    } catch (SQLException e) {
      LOG.error("Failed to delete old indexed periods", e);
      throw new RuntimeException("Failed to delete old indexed periods", e);
    }
  }

  private static IndexedPeriod mapRow(ResultSet rs) throws SQLException {
    return new IndexedPeriod(
        rs.getString("player"),
        rs.getString("platform"),
        rs.getString("year_month"),
        rs.getTimestamp("fetched_at").toInstant(),
        rs.getBoolean("is_complete"),
        rs.getInt("games_count"));
  }
}
