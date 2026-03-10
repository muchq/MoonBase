package com.muchq.games.one_d4.e2e;

import com.muchq.games.one_d4.db.IndexedPeriodDao;
import com.muchq.games.one_d4.db.IndexedPeriodStore;
import io.micronaut.context.annotation.Replaces;
import io.micronaut.context.annotation.Value;
import jakarta.inject.Singleton;
import java.time.Instant;
import java.util.Optional;
import java.util.concurrent.CountDownLatch;
import javax.sql.DataSource;

/**
 * Wraps the real {@link IndexedPeriodDao} with latch-based blocking on {@code upsertPeriod}. When
 * armed, the first upsertPeriod call signals {@code upsertReached} then blocks on {@code
 * proceedWithUpsert}, giving the test explicit control over the concurrency window.
 */
@Singleton
@Replaces(IndexedPeriodStore.class)
public class LatchIndexedPeriodDao implements IndexedPeriodStore {

  private final IndexedPeriodDao delegate;
  private volatile CountDownLatch upsertReached;
  private volatile CountDownLatch proceedWithUpsert;

  public LatchIndexedPeriodDao(
      DataSource dataSource,
      @Value("${indexer.db.url:jdbc:h2:mem:indexer;DB_CLOSE_DELAY=-1}") String jdbcUrl) {
    boolean useH2 = jdbcUrl.contains(":h2:");
    this.delegate = new IndexedPeriodDao(dataSource, useH2);
  }

  public void armUpsert(CountDownLatch upsertReached, CountDownLatch proceedWithUpsert) {
    this.upsertReached = upsertReached;
    this.proceedWithUpsert = proceedWithUpsert;
  }

  public DataSource getDataSource() {
    return delegate.getDataSource();
  }

  @Override
  public void upsertPeriod(
      String player,
      String platform,
      String month,
      Instant fetchedAt,
      boolean isComplete,
      int gamesCount,
      boolean excludeBullet) {
    CountDownLatch reached = this.upsertReached;
    CountDownLatch proceed = this.proceedWithUpsert;
    if (reached != null && proceed != null) {
      this.upsertReached = null;
      this.proceedWithUpsert = null;
      reached.countDown();
      try {
        proceed.await();
      } catch (InterruptedException e) {
        Thread.currentThread().interrupt();
      }
    }
    delegate.upsertPeriod(player, platform, month, fetchedAt, isComplete, gamesCount, excludeBullet);
  }

  @Override
  public Optional<IndexedPeriod> findCompletePeriod(
      String player, String platform, String month, boolean excludeBullet) {
    return delegate.findCompletePeriod(player, platform, month, excludeBullet);
  }

  @Override
  public int deleteOlderThan(Instant threshold) {
    return delegate.deleteOlderThan(threshold);
  }
}
