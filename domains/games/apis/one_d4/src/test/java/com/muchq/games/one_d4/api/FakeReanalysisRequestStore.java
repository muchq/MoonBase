package com.muchq.games.one_d4.api;

import com.muchq.games.one_d4.db.ReanalysisRequestStore;
import java.util.LinkedHashMap;
import java.util.Map;
import java.util.Optional;
import java.util.UUID;

/**
 * One live pass at a time, which is all {@code AdminController} is written against. What the
 * database does when two inserts race for that slot is {@code ReanalysisRequestDao}'s, pinned in
 * {@code ReanalysisRequestDaoTest} and {@code PostgresSingleLiveReanalysisTest} — the controller
 * suite needs a pass to enqueue, not a schema.
 */
final class FakeReanalysisRequestStore implements ReanalysisRequestStore {

  private final Map<UUID, ReanalysisRequest> passes = new LinkedHashMap<>();

  @Override
  public EnqueueResult enqueue() {
    Optional<ReanalysisRequest> live =
        passes.values().stream().filter(FakeReanalysisRequestStore::isLive).findFirst();
    if (live.isPresent()) {
      return new EnqueueResult(live.get(), false);
    }
    ReanalysisRequest created = new ReanalysisRequest(UUID.randomUUID(), "PENDING", 0, 0, null);
    passes.put(created.id(), created);
    return new EnqueueResult(created, true);
  }

  @Override
  public Optional<ReanalysisRequest> findById(UUID id) {
    return Optional.ofNullable(passes.get(id));
  }

  /** What the worker's checkpointed progress looks like from the API side. */
  void checkpoint(UUID id, String status, int gamesProcessed, int gamesFailed) {
    passes.put(id, new ReanalysisRequest(id, status, gamesProcessed, gamesFailed, null));
  }

  private static boolean isLive(ReanalysisRequest pass) {
    return "PENDING".equals(pass.status()) || "PROCESSING".equals(pass.status());
  }
}
