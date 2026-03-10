package com.muchq.games.one_d4.db;

import java.util.List;
import java.util.Optional;
import java.util.UUID;
import org.jdbi.v3.core.Jdbi;
import org.jdbi.v3.core.mapper.RowMapper;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

public class IndexingRequestDao implements IndexingRequestStore {
  private static final Logger LOG = LoggerFactory.getLogger(IndexingRequestDao.class);

  private static final RowMapper<IndexingRequest> ROW_MAPPER =
      (rs, ctx) ->
          new IndexingRequest(
              UUID.fromString(rs.getString("id")),
              rs.getString("player"),
              rs.getString("platform"),
              rs.getString("start_month"),
              rs.getString("end_month"),
              rs.getString("status"),
              rs.getTimestamp("created_at").toInstant(),
              rs.getTimestamp("updated_at").toInstant(),
              rs.getString("error_message"),
              rs.getInt("games_indexed"),
              rs.getBoolean("exclude_bullet"));

  private final Jdbi jdbi;

  public IndexingRequestDao(Jdbi jdbi) {
    this.jdbi = jdbi;
  }

  @Override
  public UUID create(
      String player, String platform, String startMonth, String endMonth, boolean excludeBullet) {
    UUID id = UUID.randomUUID();
    jdbi.useHandle(
        h ->
            h.createUpdate(
                    """
                    INSERT INTO indexing_requests (id, player, platform, start_month, end_month, exclude_bullet)
                    VALUES (?, ?, ?, ?, ?, ?)
                    """)
                .bind(0, id)
                .bind(1, player)
                .bind(2, platform)
                .bind(3, startMonth)
                .bind(4, endMonth)
                .bind(5, excludeBullet)
                .execute());
    return id;
  }

  @Override
  public Optional<IndexingRequest> findById(UUID id) {
    return jdbi.withHandle(
        h ->
            h.createQuery("SELECT * FROM indexing_requests WHERE id = ?")
                .bind(0, id)
                .map(ROW_MAPPER)
                .findFirst());
  }

  @Override
  public Optional<IndexingRequest> findExistingRequest(
      String player, String platform, String startMonth, String endMonth, boolean excludeBullet) {
    return jdbi.withHandle(
        h ->
            h.createQuery(
                    """
                    SELECT * FROM indexing_requests
                    WHERE player = ? AND platform = ? AND start_month = ? AND end_month = ?
                      AND exclude_bullet = ?
                      AND status IN ('PENDING', 'PROCESSING')
                    ORDER BY created_at ASC
                    LIMIT 1
                    """)
                .bind(0, player)
                .bind(1, platform)
                .bind(2, startMonth)
                .bind(3, endMonth)
                .bind(4, excludeBullet)
                .map(ROW_MAPPER)
                .findFirst());
  }

  @Override
  public List<IndexingRequest> listRecent(int limit) {
    return jdbi.withHandle(
        h ->
            h.createQuery("SELECT * FROM indexing_requests ORDER BY created_at DESC LIMIT ?")
                .bind(0, limit)
                .map(ROW_MAPPER)
                .list());
  }

  @Override
  public void updateStatus(UUID id, String status, String errorMessage, int gamesIndexed) {
    jdbi.useHandle(
        h ->
            h.createUpdate(
                    """
                    UPDATE indexing_requests
                    SET status = ?, error_message = ?, games_indexed = ?, updated_at = now()
                    WHERE id = ?
                    """)
                .bind(0, status)
                .bind(1, errorMessage)
                .bind(2, gamesIndexed)
                .bind(3, id)
                .execute());
  }
}
