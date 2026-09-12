package com.muchq.games.one_d4.db;

import java.util.Optional;
import java.util.UUID;
import org.jspecify.annotations.Nullable;

/**
 * The API side of {@code reanalysis_requests}: enqueue a pass, read one back. Claiming, leases and
 * every write past that live in the C++ worker — this seam must never grow them.
 *
 * <p>An interface for the same reason {@link GameFeatureStore} and {@link IndexingRequestStore} are
 * ones: {@code AdminController} needs a pass to enqueue, not a database. What the enqueue does
 * about a second live pass is {@link ReanalysisRequestDao}'s and is pinned against real Postgres;
 * the controller's own tests take a fake.
 */
public interface ReanalysisRequestStore {

  record ReanalysisRequest(
      UUID id, String status, int gamesProcessed, int gamesFailed, @Nullable String errorMessage) {}

  /** What enqueue answered with, and whether it made the row or found it. */
  record EnqueueResult(ReanalysisRequest request, boolean created) {}

  /**
   * The live pass, or a fresh {@code PENDING} one. One pass walks the whole corpus, so a second
   * live row buys nothing: a caller asking for one while another runs is answered with the pass
   * already doing what was asked.
   */
  EnqueueResult enqueue();

  Optional<ReanalysisRequest> findById(UUID id);
}
