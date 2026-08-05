package com.muchq.games.one_d4.api;

import com.github.benmanes.caffeine.cache.Cache;
import com.github.benmanes.caffeine.cache.Caffeine;
import com.muchq.games.one_d4.api.dto.QueryRequest;
import com.muchq.games.one_d4.api.dto.QueryResponse;
import jakarta.inject.Inject;
import jakarta.inject.Singleton;
import java.time.Clock;
import java.time.Duration;
import java.util.Optional;
import java.util.concurrent.TimeUnit;

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
 *
 * <p>Backed by a single-entry Caffeine cache: Caffeine owns the expiry bookkeeping; this class owns
 * what is cacheable ({@link #matches}) and the freshness policy.
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

  private static final String KEY = "first-page";

  private final Cache<String, QueryResponse> cache;

  @Inject
  public FirstPageCache(Clock clock) {
    this(clock, MAX_AGE);
  }

  FirstPageCache(Clock clock, Duration maxAge) {
    this.cache =
        Caffeine.newBuilder()
            .expireAfterWrite(maxAge)
            .ticker(() -> TimeUnit.MILLISECONDS.toNanos(clock.millis()))
            .build();
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

  /** The cached response, or empty if nothing has been stored or the snapshot has expired. */
  public Optional<QueryResponse> get() {
    return Optional.ofNullable(cache.getIfPresent(KEY));
  }

  public void put(QueryResponse response) {
    cache.put(KEY, response);
  }
}
