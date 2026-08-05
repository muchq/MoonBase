package com.muchq.games.one_d4.api;

import com.muchq.games.one_d4.db.GameFeatureStore;
import io.micronaut.context.annotation.Context;
import io.micronaut.context.annotation.Requires;
import io.micronaut.scheduling.annotation.Scheduled;

/**
 * Keeps {@link FirstPageCache} warm by refreshing it on a schedule.
 *
 * <p>The schedule is the point, not an optimization of a read-through cache: on a low-traffic site
 * a lazily-populated cache misses for exactly the visitor it exists for — the first one after a
 * quiet stretch. Refreshing every 30 seconds keeps the snapshot always ready and bounds staleness
 * to well under {@link FirstPageCache#MAX_AGE}, so no write-path invalidation is needed even with
 * other JVMs writing to the shared database.
 */
// @Requires because this bean is eager: test contexts that boot the api package without the
// database layer (e.g. DtoJsonCompatTest, which only wants the container's ObjectMapper) must not
// be forced to satisfy the cache loader's store dependency. FirstPageWarmupTest pins that the
// bean does activate — and fires — in the real application context.
@Context
@Requires(beans = GameFeatureStore.class)
public class FirstPageWarmer {

  private final FirstPageCache cache;

  public FirstPageWarmer(FirstPageCache cache) {
    this.cache = cache;
  }

  // fixedDelay must stay at half FirstPageCache.MAX_AGE or less (pinned by
  // FirstPageWarmerTest), or the cache expires between refreshes and every first load falls
  // through to the database again. fixedDelay measures from the previous run's completion, so
  // the true period is 30s plus query time; the 2x headroom in MAX_AGE absorbs that.
  // refreshNow() keeps the last good snapshot and logs on failure, so a failed tick never
  // cancels the schedule. A query that hangs (rather than throws) does block future ticks —
  // the cache's load-on-miss bounds the damage to serving live until a restart.
  @Scheduled(fixedDelay = "30s", initialDelay = "1s")
  public void refresh() {
    cache.refreshNow();
  }
}
