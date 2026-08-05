package com.muchq.games.one_d4.api;

import com.muchq.games.one_d4.db.GameFeatureStore;
import io.micronaut.context.annotation.Context;
import io.micronaut.context.annotation.Requires;
import io.micronaut.scheduling.annotation.Scheduled;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

/**
 * Keeps {@link FirstPageCache} warm by re-running the default first-load query on a schedule.
 *
 * <p>The schedule is the point, not an optimization of a read-through cache: on a low-traffic site
 * a lazily-populated cache misses for exactly the visitor it exists for — the first one after a
 * quiet stretch. Refreshing every 30 seconds keeps the snapshot always ready and bounds staleness
 * to well under {@link FirstPageCache#MAX_AGE}, so no write-path invalidation is needed even with
 * other JVMs writing to the shared database.
 */
// @Requires because this bean is eager: test contexts that boot the api package without the
// database layer (e.g. DtoJsonCompatTest, which only wants the container's ObjectMapper) must not
// be forced to satisfy QueryExecutor's store dependency. FirstPageWarmupTest pins that the bean
// does activate — and fires — in the real application context.
@Context
@Requires(beans = GameFeatureStore.class)
public class FirstPageWarmer {
  private static final Logger LOG = LoggerFactory.getLogger(FirstPageWarmer.class);

  private final QueryExecutor queryExecutor;
  private final FirstPageCache cache;

  public FirstPageWarmer(QueryExecutor queryExecutor, FirstPageCache cache) {
    this.queryExecutor = queryExecutor;
    this.cache = cache;
  }

  // fixedDelay must stay at half FirstPageCache.MAX_AGE or less (pinned by
  // FirstPageWarmerTest), or the cache expires between refreshes and every first load falls
  // through to the database again. fixedDelay measures from the previous run's completion, so
  // the true period is 30s plus query time; the 2x headroom in MAX_AGE absorbs that.
  @Scheduled(fixedDelay = "30s", initialDelay = "1s")
  public void refresh() {
    try {
      cache.put(queryExecutor.execute(FirstPageCache.defaultRequest()));
    } catch (Exception e) {
      // Swallow so a failed tick doesn't cancel the schedule; a stale snapshot ages out via
      // MAX_AGE and requests fall back to the live query path. A query that hangs (rather than
      // throws) does block future ticks — the same fall-through bounds the damage to serving
      // live until a restart.
      LOG.warn("Failed to refresh first-page cache", e);
    }
  }
}
