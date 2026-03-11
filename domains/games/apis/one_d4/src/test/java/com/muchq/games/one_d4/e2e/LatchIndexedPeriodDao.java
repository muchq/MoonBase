package com.muchq.games.one_d4.e2e;

import com.muchq.games.one_d4.db.IndexedPeriodDao;
import com.muchq.games.one_d4.db.IndexedPeriodStore;
import io.micronaut.context.annotation.Replaces;
import io.micronaut.context.annotation.Value;
import jakarta.inject.Singleton;
import java.time.Instant;
import java.util.Optional;
import java.util.concurrent.CountDownLatch;
import org.jdbi.v3.core.Jdbi;

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
  private volatile CountDownLatch retryReached;
  private volatile CountDownLatch proceedWithRetry;

  public LatchIndexedPeriodDao(
      Jdbi jdbi, @Value("${indexer.db.url:jdbc:h2:mem:indexer;DB_CLOSE_DELAY=-1}") String jdbcUrl) {
    boolean useH2 = jdbcUrl.contains(":h2:");
    this.delegate = new IndexedPeriodDao(jdbi, useH2);
  }

  /**
   * Arms synchronization for both the first upsertPeriod attempt and its retry.
   *
   * <p>The first call signals {@code upsertReached} and blocks on {@code proceedWithUpsert}, giving
   * the test a window to hold a write lock before the delegate is invoked (expected to fail). The
   * retry signals {@code retryReached} and blocks on {@code proceedWithRetry}, giving the test a
   * window to release the lock so the delegate succeeds.
   */
  public void armUpsert(
      CountDownLatch upsertReached,
      CountDownLatch proceedWithUpsert,
      CountDownLatch retryReached,
      CountDownLatch proceedWithRetry) {
    this.upsertReached = upsertReached;
    this.proceedWithUpsert = proceedWithUpsert;
    this.retryReached = retryReached;
    this.proceedWithRetry = proceedWithRetry;
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
      awaitUninterruptibly(proceed);
    } else {
      CountDownLatch retryR = this.retryReached;
      CountDownLatch retryP = this.proceedWithRetry;
      if (retryR != null && retryP != null) {
        this.retryReached = null;
        this.proceedWithRetry = null;
        retryR.countDown();
        awaitUninterruptibly(retryP);
      }
    }
    delegate.upsertPeriod(
        player, platform, month, fetchedAt, isComplete, gamesCount, excludeBullet);
  }

  private static void awaitUninterruptibly(CountDownLatch latch) {
    try {
      latch.await();
    } catch (InterruptedException e) {
      Thread.currentThread().interrupt();
    }
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
