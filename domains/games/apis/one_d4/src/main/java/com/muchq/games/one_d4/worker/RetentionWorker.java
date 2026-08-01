package com.muchq.games.one_d4.worker;

import com.muchq.games.one_d4.db.GameFeatureStore;
import com.muchq.games.one_d4.db.IndexedPeriodStore;
import com.muchq.games.one_d4.db.IndexingRequestStore;
import com.muchq.games.one_d4.db.RetentionPolicy;
import io.micronaut.context.annotation.Context;
import io.micronaut.scheduling.annotation.Scheduled;
import jakarta.inject.Inject;
import java.time.Clock;
import java.time.Duration;
import java.time.Instant;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

@Context
public class RetentionWorker {
  private static final Logger LOG = LoggerFactory.getLogger(RetentionWorker.class);
  private static final Duration RETENTION_PERIOD = RetentionPolicy.PERIOD;

  private static final Duration REQUEST_RETENTION_PERIOD = RetentionPolicy.REQUEST;
  private static final Duration STALE_REQUEST_PERIOD = RetentionPolicy.STALE_REQUEST;

  private final GameFeatureStore gameFeatureStore;
  private final IndexedPeriodStore indexedPeriodStore;
  private final IndexingRequestStore indexingRequestStore;
  private final Clock clock;

  public RetentionWorker(
      GameFeatureStore gameFeatureStore,
      IndexedPeriodStore indexedPeriodStore,
      IndexingRequestStore indexingRequestStore) {
    this(gameFeatureStore, indexedPeriodStore, indexingRequestStore, Clock.systemUTC());
  }

  /**
   * @param clock injected so a test can advance past the retention boundary and exercise the
   *     threshold arithmetic itself. Backdating rows instead only ever tests {@code
   *     deleteOlderThan} — with rows at the epoch, any positive retention period passes, so the
   *     window could be changed to 700 days without a single failure.
   */
  @Inject
  public RetentionWorker(
      GameFeatureStore gameFeatureStore,
      IndexedPeriodStore indexedPeriodStore,
      IndexingRequestStore indexingRequestStore,
      Clock clock) {
    this.gameFeatureStore = gameFeatureStore;
    this.indexedPeriodStore = indexedPeriodStore;
    this.indexingRequestStore = indexingRequestStore;
    this.clock = clock;
    LOG.info(
        "RetentionWorker initialized (games/periods={} days, requests={} days, stale requests={},"
            + " interval=1h)",
        RETENTION_PERIOD.toDays(),
        REQUEST_RETENTION_PERIOD.toDays(),
        STALE_REQUEST_PERIOD);
  }

  @Scheduled(fixedDelay = "1h", initialDelay = "1m")
  public void runRetention() {
    LOG.info(
        "Running retention policy (games/periods={} days, requests={} days)",
        RETENTION_PERIOD.toDays(),
        REQUEST_RETENTION_PERIOD.toDays());
    Instant now = clock.instant();
    Instant threshold = now.minus(RETENTION_PERIOD);
    try {
      // Settle abandoned requests before deleting anything. This is the only unconditional caller:
      // the submit path reclaims too, but only the one tuple being submitted, so a row nobody
      // re-submits would otherwise sit unsettled until it aged out of the request window entirely
      // — never returned to the queue if its owner died, and never answered if the fleet did.
      int reclaimed = indexingRequestStore.reclaimStale(STALE_REQUEST_PERIOD, now);

      // Games before requests, because game_features.request_id is a foreign key onto
      // indexing_requests(id). The delete itself is guarded — deleteOlderThan skips any request
      // still referenced — so reversing these would not corrupt anything or raise a violation; it
      // would just leave a qualifying request behind for a later pass, once its games had gone.
      // Ordering is what lets a request and the games it owns clear in the same pass rather than
      // over two.
      int games = gameFeatureStore.deleteOlderThan(threshold);
      int periods = indexedPeriodStore.deleteOlderThan(threshold);
      int requests = indexingRequestStore.deleteOlderThan(now.minus(REQUEST_RETENTION_PERIOD));

      // "Settled", not "retired": since #1279 most of this count is requests returned to the
      // queue for another worker, not requests given up on. The DAO logs the split.
      LOG.info(
          "Retention cleanup complete: deleted {} games, {} periods, {} requests; settled {}"
              + " abandoned requests",
          games,
          periods,
          requests,
          reclaimed);
    } catch (Exception e) {
      LOG.error("Failed to run retention policy", e);
    }
  }
}
