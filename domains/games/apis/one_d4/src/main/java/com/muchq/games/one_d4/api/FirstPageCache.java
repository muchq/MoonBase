package com.muchq.games.one_d4.api;

import com.muchq.games.one_d4.api.dto.QueryRequest;
import com.muchq.games.one_d4.api.dto.QueryResponse;
import jakarta.inject.Inject;
import jakarta.inject.Singleton;
import java.time.Clock;
import java.time.Duration;
import java.time.Instant;
import java.util.Optional;
import java.util.concurrent.atomic.AtomicReference;
import org.jspecify.annotations.Nullable;

/**
 * In-memory snapshot of the response to the query 1d4_web's GamesView fires on first page load, so
 * the first paint is served without touching the database.
 *
 * <p>Freshness is bounded by {@link #MAX_AGE} rather than by write invalidation: writers include
 * index workers on other JVMs sharing the same database, which this process cannot observe, so a
 * short time bound is the honest guarantee. {@link FirstPageWarmer} refreshes the snapshot on a
 * schedule; a snapshot older than two refresh intervals means the warmer is dead or the database is
 * down, and {@link #get()} then returns empty so the caller falls back to the live path instead of
 * serving arbitrarily old data.
 */
@Singleton
public class FirstPageCache {
  /** Must match DEFAULT_QUERY in 1d4_web's GamesView.tsx — that is the request being cached. */
  public static final String DEFAULT_QUERY = "num.moves >= 0";

  /** Must match DEFAULT_PAGE_SIZE in 1d4_web's GamesView.tsx. */
  public static final int DEFAULT_LIMIT = 25;

  /**
   * Two {@link FirstPageWarmer} refresh intervals: one missed tick tolerated, not a dead warmer.
   */
  static final Duration MAX_AGE = Duration.ofSeconds(60);

  private final Clock clock;
  private final Duration maxAge;
  private final AtomicReference<@Nullable Snapshot> snapshot = new AtomicReference<>();

  @Inject
  public FirstPageCache(Clock clock) {
    this(clock, MAX_AGE);
  }

  FirstPageCache(Clock clock, Duration maxAge) {
    this.clock = clock;
    this.maxAge = maxAge;
  }

  /** The exact request GamesView sends on first load. */
  public static QueryRequest defaultRequest() {
    return new QueryRequest(DEFAULT_QUERY, DEFAULT_LIMIT, 0, null);
  }

  /**
   * Whether this request is the first-load default. Exact match on all four fields: anything else —
   * another page, another page size, a player perspective — is a different result set and must go
   * to the database.
   */
  public boolean matches(QueryRequest request) {
    return request.query() != null
        && DEFAULT_QUERY.equals(request.query().strip())
        && request.limit() == DEFAULT_LIMIT
        && request.offset() == 0
        && (request.player() == null || request.player().isBlank());
  }

  /** The cached response, or empty if nothing has been stored or the snapshot is too old. */
  public Optional<QueryResponse> get() {
    Snapshot current = snapshot.get();
    if (current == null) {
      return Optional.empty();
    }
    if (Duration.between(current.refreshedAt(), clock.instant()).compareTo(maxAge) > 0) {
      return Optional.empty();
    }
    return Optional.of(current.response());
  }

  public void put(QueryResponse response) {
    snapshot.set(new Snapshot(response, clock.instant()));
  }

  private record Snapshot(QueryResponse response, Instant refreshedAt) {}
}
