package com.muchq.platform.yodel;

import static org.assertj.core.api.Assertions.assertThat;
import static org.assertj.core.api.Assertions.assertThatThrownBy;

import java.util.List;
import java.util.Map;
import java.util.concurrent.CountDownLatch;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;
import java.util.concurrent.TimeUnit;
import org.junit.jupiter.api.Test;

public class CustomMetricsTest {

  @Test
  public void aCounterAccumulatesPerLabelSet() {
    CustomMetrics metrics = new CustomMetrics();
    metrics.increment("games_indexed", Map.of("platform", "chess_com"));
    metrics.add("games_indexed", 4, Map.of("platform", "chess_com"));
    metrics.increment("games_indexed", Map.of("platform", "lichess"));

    assertThat(metrics.counterSnapshot())
        .extracting(
            CustomMetrics.CounterSnapshot::name,
            s -> s.labels().get("platform"),
            CustomMetrics.CounterSnapshot::value)
        .containsExactly(
            org.assertj.core.groups.Tuple.tuple("games_indexed", "chess_com", 5L),
            org.assertj.core.groups.Tuple.tuple("games_indexed", "lichess", 1L));
  }

  /**
   * The same pairs in a different order are the same series. Without normalisation they would be
   * two, and a dashboard summing one of them would silently report half the traffic.
   */
  @Test
  public void labelOrderDoesNotSplitASeries() {
    CustomMetrics metrics = new CustomMetrics();
    metrics.increment("runs", Map.of("outcome", "completed", "stage", "flush"));
    metrics.increment("runs", Map.of("stage", "flush", "outcome", "completed"));

    assertThat(metrics.counterSnapshot()).hasSize(1);
    assertThat(metrics.counterSnapshot().get(0).value()).isEqualTo(2L);
  }

  @Test
  public void aDistributionRecordsSumCountAndBuckets() {
    CustomMetrics metrics = new CustomMetrics();
    metrics.record("month_duration_micros", 30.0);
    metrics.record("month_duration_micros", 70.0);

    List<CustomMetrics.DistributionSnapshot> snapshots = metrics.distributionSnapshot();
    assertThat(snapshots).hasSize(1);
    CustomMetrics.DistributionSnapshot d = snapshots.get(0);
    assertThat(d.count()).isEqualTo(2L);
    assertThat(d.sum()).isEqualTo(100.0);
    // Bounds are {0,5,10,25,50,75,...}: 30 lands in the "<=50" bucket, 70 in "<=75".
    assertThat(d.bucketCounts()[HttpServerMetrics.BUCKET_BOUNDS.length]).isZero();
    assertThat(java.util.Arrays.stream(d.bucketCounts()).sum()).isEqualTo(2L);
  }

  @Test
  public void anObservationBeyondTheLastBoundLandsInTheOverflowBucket() {
    CustomMetrics metrics = new CustomMetrics();
    metrics.record("month_duration_micros", 999_999.0);

    long[] buckets = metrics.distributionSnapshot().get(0).bucketCounts();
    assertThat(buckets[HttpServerMetrics.BUCKET_BOUNDS.length])
        .as("a value past the largest bound must still be counted, not dropped")
        .isEqualTo(1L);
  }

  /**
   * A name the Prometheus exporter cannot represent is not a small problem: it records fine,
   * exports fine, and then matches no dashboard query. Failing at the call site is the only place
   * the mistake is still cheap.
   */
  @Test
  public void aNameThatCouldNotSurviveExportIsRejectedAtTheCallSite() {
    CustomMetrics metrics = new CustomMetrics();
    assertThatThrownBy(() -> metrics.increment("games.indexed"))
        .isInstanceOf(IllegalArgumentException.class)
        .hasMessageContaining("games.indexed");
    assertThatThrownBy(() -> metrics.increment("runs", Map.of("out come", "x")))
        .isInstanceOf(IllegalArgumentException.class);
  }

  @Test
  public void aCounterRefusesToGoBackwards() {
    CustomMetrics metrics = new CustomMetrics();
    assertThatThrownBy(() -> metrics.add("games_indexed", -1, Map.of()))
        .isInstanceOf(IllegalArgumentException.class)
        .hasMessageContaining("may not decrease");
  }

  @Test
  public void anUntouchedRegistryIsEmptySoTheEncoderCanSkipIt() {
    CustomMetrics metrics = new CustomMetrics();
    assertThat(metrics.isEmpty()).isTrue();
    metrics.increment("games_indexed");
    assertThat(metrics.isEmpty()).isFalse();
  }

  /** Recording happens on request threads and worker threads at once; nothing may be lost. */
  @Test
  public void concurrentRecordingLosesNothing() throws Exception {
    CustomMetrics metrics = new CustomMetrics();
    int threads = 8;
    int perThread = 1000;
    ExecutorService pool = Executors.newFixedThreadPool(threads);
    CountDownLatch go = new CountDownLatch(1);
    try {
      for (int t = 0; t < threads; t++) {
        pool.submit(
            () -> {
              go.await();
              for (int i = 0; i < perThread; i++) {
                metrics.increment("games_indexed", Map.of("platform", "chess_com"));
                metrics.record("month_duration_micros", 1.0);
              }
              return null;
            });
      }
      go.countDown();
      pool.shutdown();
      assertThat(pool.awaitTermination(30, TimeUnit.SECONDS)).isTrue();
    } finally {
      pool.shutdownNow();
    }

    assertThat(metrics.counterSnapshot().get(0).value()).isEqualTo((long) threads * perThread);
    assertThat(metrics.distributionSnapshot().get(0).count()).isEqualTo((long) threads * perThread);
  }
}
