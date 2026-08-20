package com.muchq.games.one_d4.db;

import java.util.Optional;
import java.util.UUID;
import org.jdbi.v3.core.Jdbi;
import org.jspecify.annotations.Nullable;

/**
 * The API side of {@code reanalysis_requests}: enqueue a pass, read one back. Claiming, leases and
 * every write past that live in the C++ worker — this class must never grow them.
 */
public class ReanalysisRequestDao {

  public record ReanalysisRequest(
      UUID id, String status, int gamesProcessed, int gamesFailed, @Nullable String errorMessage) {}

  /** What enqueue answered with, and whether it made the row or found it. */
  public record EnqueueResult(ReanalysisRequest request, boolean created) {}

  private static final String SELECT_COLUMNS =
      "SELECT id, status, games_processed, games_failed, error_message FROM reanalysis_requests ";

  private final Jdbi jdbi;

  public ReanalysisRequestDao(Jdbi jdbi) {
    this.jdbi = jdbi;
  }

  /**
   * The live pass, or a fresh {@code PENDING} one. One pass walks the whole corpus, so a second
   * live row buys nothing — {@code idx_reanalysis_requests_single_live} refuses it at insert, and
   * this answers with the pass already doing what was asked instead of surfacing that refusal.
   *
   * <p>Check-then-insert, with the insert re-checking on a unique violation: two racers can both
   * see no live row, but the index lets only one insert land. The loser returns the winner's row.
   * (H2 carries a non-unique stand-in for the index, so the race backstop is a Postgres behavior —
   * {@code PostgresSingleLiveReanalysisTest} is where it has teeth.)
   */
  public EnqueueResult enqueue() {
    Optional<ReanalysisRequest> live = findLive();
    if (live.isPresent()) {
      return new EnqueueResult(live.get(), false);
    }
    try {
      // Generated keys rather than RETURNING, which H2 does not parse.
      UUID id =
          jdbi.withHandle(
              h ->
                  h.createUpdate("INSERT INTO reanalysis_requests (status) VALUES ('PENDING')")
                      .executeAndReturnGeneratedKeys("id")
                      .mapTo(UUID.class)
                      .one());
      return new EnqueueResult(findById(id).orElseThrow(), true);
    } catch (RuntimeException e) {
      // The single-live index said no: somebody else's insert landed between
      // our check and ours. Their pass is the answer.
      Optional<ReanalysisRequest> winner = findLive();
      if (winner.isPresent()) {
        return new EnqueueResult(winner.get(), false);
      }
      throw e;
    }
  }

  public Optional<ReanalysisRequest> findById(UUID id) {
    return jdbi.withHandle(
        h ->
            h.createQuery(SELECT_COLUMNS + "WHERE id = :id")
                .bind("id", id)
                .map(ReanalysisRequestDao::toRequest)
                .findOne());
  }

  private Optional<ReanalysisRequest> findLive() {
    return jdbi.withHandle(
        h ->
            h.createQuery(SELECT_COLUMNS + "WHERE status IN ('PENDING', 'PROCESSING') LIMIT 1")
                .map(ReanalysisRequestDao::toRequest)
                .findOne());
  }

  private static ReanalysisRequest toRequest(
      java.sql.ResultSet rs, org.jdbi.v3.core.statement.StatementContext ctx)
      throws java.sql.SQLException {
    return new ReanalysisRequest(
        rs.getObject("id", UUID.class),
        rs.getString("status"),
        rs.getInt("games_processed"),
        rs.getInt("games_failed"),
        rs.getString("error_message"));
  }
}
