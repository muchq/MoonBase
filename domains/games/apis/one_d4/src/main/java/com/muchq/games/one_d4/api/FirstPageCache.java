package com.muchq.games.one_d4.api;

import com.github.benmanes.caffeine.cache.Caffeine;
import com.github.benmanes.caffeine.cache.LoadingCache;
import com.github.benmanes.caffeine.cache.Ticker;
import com.muchq.games.one_d4.api.dto.QueryRequest;
import com.muchq.games.one_d4.api.dto.QueryResponse;
import jakarta.inject.Inject;
import jakarta.inject.Singleton;
import java.time.Duration;
import java.util.Optional;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

/**
 * In-memory snapshot of the response to the query 1d4_web's GamesView fires on first page load, so
 * the first paint is served without touching the database.
 *
 * <p>Freshness is bounded by {@link #MAX_AGE} rather than by write invalidation: writers include
 * index workers on other JVMs sharing the same database, which this process cannot observe, so a
 * short time bound is the honest guarantee. {@link FirstPageWarmer} calls {@link #refreshNow()} on
 * a schedule; if the warmer is dead or was never scheduled, {@link #get()} loads on demand — under
 * Caffeine's per-key lock, so concurrent cold misses collapse to a single query.
 *
 * <p>Backed by a single-entry Caffeine {@link LoadingCache}: Caffeine owns expiry, loading, and
 * single-flight; this class owns what is cacheable ({@link #matches}) and the freshness policy. The
 * loader is the one place that knows how a snapshot is computed — the same {@link QueryExecutor} a
 * live request runs. Production uses Caffeine's monotonic system ticker ({@link Ticker}'s contract
 * is a monotonic source, which a wall clock is not); tests inject their own.
 */
@Singleton
public class FirstPageCache {
  private static final Logger LOG = LoggerFactory.getLogger(FirstPageCache.class);

  /** Must match DEFAULT_QUERY in 1d4_web's GamesView.tsx — that is the request being cached. */
  public static final String DEFAULT_QUERY = "num.moves >= 0";

  /** Must match DEFAULT_PAGE_SIZE in 1d4_web's GamesView.tsx. */
  public static final int DEFAULT_LIMIT = 25;

  /**
   * Two {@link FirstPageWarmer} refresh intervals: one missed tick tolerated, not a dead warmer.
   * FirstPageWarmerTest pins the 2x relationship against the annotation.
   */
  static final Duration MAX_AGE = Duration.ofSeconds(60);

  private static final String KEY = "first-page";

  private final LoadingCache<String, QueryResponse> cache;
  private final QueryExecutor queryExecutor;

  @Inject
  public FirstPageCache(QueryExecutor queryExecutor) {
    this(Ticker.systemTicker(), MAX_AGE, queryExecutor);
  }

  FirstPageCache(Ticker ticker, Duration maxAge, QueryExecutor queryExecutor) {
    this.queryExecutor = queryExecutor;
    this.cache =
        Caffeine.newBuilder()
            .expireAfterWrite(maxAge)
            .ticker(ticker)
            // Same-thread executor: Caffeine's maintenance work runs on the calling thread,
            // keeping tests deterministic.
            .executor(Runnable::run)
            .build(key -> loadSnapshot());
  }

  /** The one definition of how a snapshot is computed — used by get()'s loader and refreshNow(). */
  private QueryResponse loadSnapshot() {
    return queryExecutor.execute(defaultRequest());
  }

  /** The exact request GamesView sends on first load. */
  public static QueryRequest defaultRequest() {
    return new QueryRequest(DEFAULT_QUERY, DEFAULT_LIMIT, 0, null);
  }

  /**
   * Whether this request is the first-load default: the default query string after trimming, the
   * default page size, offset 0, and no player (blank counts as absent). Anything else — another
   * page, another page size, a non-blank player — is treated as a different result set and must go
   * to the database. Player cannot actually change this query's results today (it has no
   * perspective fields), but rejecting it keeps this predicate a pure function of the request
   * rather than of ChessQL semantics.
   */
  public boolean matches(QueryRequest request) {
    return request.query() != null
        && DEFAULT_QUERY.equals(request.query().strip())
        && request.limit() == DEFAULT_LIMIT
        && request.offset() == 0
        && (request.player() == null || request.player().isBlank());
  }

  /**
   * The cached response, loading it on a miss or after expiry. Loading happens under the key's
   * lock, so concurrent cold misses run one query, and a loader failure propagates to the caller
   * like any live-path query failure.
   */
  public QueryResponse get() {
    return cache.get(KEY);
  }

  /** The cached response without loading — for tests and diagnostics only. */
  Optional<QueryResponse> peek() {
    return Optional.ofNullable(cache.getIfPresent(KEY));
  }

  /**
   * Recomputes the snapshot even if it is still fresh — the warmer's tick. On failure the previous
   * snapshot is kept (until it expires) and the error is logged; nothing propagates, so the
   * scheduler keeps ticking.
   *
   * <p>Deliberately {@code put}, not {@code LoadingCache.refresh}: refresh discards a reload whose
   * entry changed underneath it, and an entry <em>expiring</em> mid-reload counts as such a change
   * — a tick whose query outlives the remaining freshness window would compute a snapshot, throw it
   * away without logging, and leave the cache cold until the next tick. A successful warm must
   * always install.
   */
  public void refreshNow() {
    try {
      cache.put(KEY, loadSnapshot());
    } catch (RuntimeException e) {
      LOG.warn("Failed to refresh first-page cache", e);
    }
  }
}
