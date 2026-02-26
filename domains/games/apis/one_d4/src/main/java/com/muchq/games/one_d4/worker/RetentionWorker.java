package com.muchq.games.one_d4.worker;

import com.muchq.games.one_d4.db.GameFeatureStore;
import com.muchq.games.one_d4.db.IndexedPeriodStore;
import io.micronaut.context.annotation.Context;
import io.micronaut.scheduling.annotation.Scheduled;
import jakarta.inject.Inject;
import java.time.Duration;
import java.time.Instant;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

@Context
public class RetentionWorker {
  private static final Logger LOG = LoggerFactory.getLogger(RetentionWorker.class);
  private static final Duration RETENTION_PERIOD = Duration.ofDays(7);

  private final GameFeatureStore gameFeatureStore;
  private final IndexedPeriodStore indexedPeriodStore;

  @Inject
  public RetentionWorker(GameFeatureStore gameFeatureStore, IndexedPeriodStore indexedPeriodStore) {
    this.gameFeatureStore = gameFeatureStore;
    this.indexedPeriodStore = indexedPeriodStore;
    LOG.info("RetentionWorker initialized (retention={} days, interval=1h)", RETENTION_PERIOD.toDays());
  }

  @Scheduled(fixedDelay = "1h", initialDelay = "1m")
  public void runRetention() {
    LOG.info("Running game retention policy ({} days)", RETENTION_PERIOD.toDays());
    Instant threshold = Instant.now().minus(RETENTION_PERIOD);
    try {
      int games = gameFeatureStore.deleteOlderThan(threshold);
      int periods = indexedPeriodStore.deleteOlderThan(threshold);
      LOG.info("Retention cleanup complete: deleted {} games, {} periods", games, periods);
    } catch (Exception e) {
      LOG.error("Failed to run retention policy", e);
    }
  }
}
