package com.muchq.games.one_d4.worker;

import com.muchq.games.one_d4.db.GameFeatureStore;
import io.micronaut.scheduling.annotation.Scheduled;
import java.time.Duration;
import java.time.Instant;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

public class RetentionWorker {
  private static final Logger LOG = LoggerFactory.getLogger(RetentionWorker.class);
  private static final Duration RETENTION_PERIOD = Duration.ofDays(7);

  private final GameFeatureStore gameFeatureStore;

  public RetentionWorker(GameFeatureStore gameFeatureStore) {
    this.gameFeatureStore = gameFeatureStore;
  }

  @Scheduled(fixedDelay = "1h", initialDelay = "1m")
  public void runRetention() {
    LOG.info("Running game retention policy ({} days)", RETENTION_PERIOD.toDays());
    Instant threshold = Instant.now().minus(RETENTION_PERIOD);
    try {
      gameFeatureStore.deleteOlderThan(threshold);
    } catch (Exception e) {
      LOG.error("Failed to run retention policy", e);
    }
  }
}
