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
   * Two callers passing the same pairs in different orders land on one series.
   *
   * <p>Weaker than it looks on its own — {@code Map.equals} is order-insensitive, so a
   * ConcurrentHashMap keyed on any map would dedup these — which is why the ordering assertion
   * below is the real subject. Sorting is what makes {@code Series.compareTo} total, and a
   * comparator that ties leaves payload order to hash iteration.
   */
  @Test
  public void labelOrderDoesNotSplitASeries() {
    CustomMetrics metrics = new CustomMetrics();
    metrics.increment("runs", Map.of("outcome", "completed", "stage", "flush"));
    metrics.increment("runs", Map.of("stage", "flush", "outcome", "completed"));

    assertThat(metrics.counterSnapshot()).hasSize(1);
    assertThat(metrics.counterSnapshot().get(0).value()).isEqualTo(2L);
  }

  /**
   * Snapshot order is a function of name and labels, not of insertion or hash order. Asserted over
   * series that differ only in a later label, since those are the ones a tie would reorder.
   */
  @Test
  public void snapshotOrderIsStableAcrossLabelSets() {
    CustomMetrics first = new CustomMetrics();
    first.increment("runs", Map.of("outcome", "failed"));
    first.increment("runs", Map.of("outcome", "completed"));
    first.increment("games", Map.of("outcome", "completed"));

    CustomMetrics second = new CustomMetrics();
    second.increment("games", Map.of("outcome", "completed"));
    second.increment("runs", Map.of("outcome", "completed"));
    second.increment("runs", Map.of("outcome", "failed"));

    assertThat(describe(first))
        .containsExactly(
            "games{outcome=completed}", "runs{outcome=completed}", "runs{outcome=failed}");
    assertThat(describe(first)).isEqualTo(describe(second));
  }

  /**
   * Two series whose label maps stringify identically stay two series, in a fixed order.
   *
   * <p>{@code {a=1, b=2}} and the single entry {@code {a="1, b=2"}} produce the same {@code
   * toString}, and label values are not validated — so a comparator built on that string ties, and
   * a tie hands ordering back to hash iteration. Compared on the label maps rather than on a
   * rendering of them, because a rendering is exactly what collapses the two.
   */
  @Test
  public void seriesThatStringifyAlikeStayDistinctAndOrderStably() {
    Map<String, String> twoLabels = Map.of("a", "1", "b", "2");
    Map<String, String> oneAmbiguousLabel = Map.of("a", "1, b=2");

    CustomMetrics first = new CustomMetrics();
    first.increment("runs", twoLabels);
    first.increment("runs", oneAmbiguousLabel);

    CustomMetrics second = new CustomMetrics();
    second.increment("runs", oneAmbiguousLabel);
    second.increment("runs", twoLabels);

    assertThat(first.counterSnapshot()).hasSize(2);
    assertThat(labelsOf(first))
        .as("distinct label sets, not one merged series")
        .containsExactlyInAnyOrder(twoLabels, oneAmbiguousLabel);
    assertThat(labelsOf(first))
        .as("and the same order regardless of which arrived first")
        .isEqualTo(labelsOf(second));
  }

  private static List<Map<String, String>> labelsOf(CustomMetrics metrics) {
    return metrics.counterSnapshot().stream().map(CustomMetrics.CounterSnapshot::labels).toList();
  }

  private static List<String> describe(CustomMetrics metrics) {
    return metrics.counterSnapshot().stream().map(s -> s.name() + s.labels()).toList();
  }

  /**
   * A snapshot hands out a copy of the labels, never the live hash key.
   *
   * <p>The key's own map decides its hash. Handing the caller the real one lets a mutation strand
   * the entry in the wrong bin: the next record for those labels mints a second entry from zero
   * while iteration still finds the old one, and the encoder emits two data points with identical
   * attributes in a single payload — which Prometheus rejects as a duplicate sample.
   */
  @Test
  public void aSnapshotDoesNotExposeTheLiveSeriesKey() {
    CustomMetrics metrics = new CustomMetrics();
    metrics.increment("runs", Map.of("outcome", "completed"));
    metrics.record("d", 1.0, Map.of("outcome", "completed"));

    assertThatThrownBy(() -> metrics.counterSnapshot().get(0).labels().put("outcome", "failed"))
        .isInstanceOf(UnsupportedOperationException.class);
    assertThatThrownBy(
            () -> metrics.distributionSnapshot().get(0).labels().put("outcome", "failed"))
        .isInstanceOf(UnsupportedOperationException.class);

    // And the registry is untouched by the attempt.
    metrics.increment("runs", Map.of("outcome", "completed"));
    assertThat(metrics.counterSnapshot()).hasSize(1);
    assertThat(metrics.counterSnapshot().get(0).value()).isEqualTo(2L);
  }

  /** A label the encoder always adds cannot also be supplied by the caller. */
  @Test
  public void serviceNameIsRejectedAsACallerLabel() {
    CustomMetrics metrics = new CustomMetrics();
    assertThatThrownBy(() -> metrics.increment("runs", Map.of("service_name", "one_d4")))
        .isInstanceOf(IllegalArgumentException.class)
        .hasMessageContaining("service_name");
  }

  /** A negative or NaN observation corrupts a cumulative _sum silently. */
  @Test
  public void aDistributionRefusesObservationsThatWouldCorruptTheSum() {
    CustomMetrics metrics = new CustomMetrics();
    assertThatThrownBy(() -> metrics.record("d", -1.0))
        .isInstanceOf(IllegalArgumentException.class);
    assertThatThrownBy(() -> metrics.record("d", Double.NaN))
        .isInstanceOf(IllegalArgumentException.class);
    assertThatThrownBy(() -> metrics.record("d", Double.POSITIVE_INFINITY))
        .isInstanceOf(IllegalArgumentException.class);
  }

  /**
   * Declared bounds are used, and the default set is not. Without this the per-distribution bounds
   * are unpinned: every observation of a minutes-scale value would silently land in the overflow
   * bucket while _sum and _count stayed correct, which is exactly the failure the feature exists to
   * prevent.
   */
  @Test
  public void aDeclaredDistributionUsesItsOwnBounds() {
    CustomMetrics metrics = new CustomMetrics();
    double[] bounds = {1_000, 1_000_000, 60_000_000};
    metrics.defineDistribution("run_micros", bounds);

    metrics.record("run_micros", 500); // <= 1_000        -> bucket 0
    metrics.record("run_micros", 30_000_000); // <= 60_000_000   -> bucket 2
    metrics.record("run_micros", 90_000_000); // overflow        -> bucket 3

    CustomMetrics.DistributionSnapshot d = metrics.distributionSnapshot().get(0);
    assertThat(d.bounds()).containsExactly(1_000.0, 1_000_000.0, 60_000_000.0);
    assertThat(d.bucketCounts()).containsExactly(1L, 0L, 1L, 1L);
    assertThat(metrics.boundsFor("run_micros")).containsExactly(1_000.0, 1_000_000.0, 60_000_000.0);
    assertThat(metrics.boundsFor("undeclared")).isEqualTo(CustomMetrics.DEFAULT_BOUNDS);
  }

  /**
   * The fallback for an undeclared distribution is not the HTTP latency layout.
   *
   * <p>It was, until #1286 reshaped {@link HttpServerMetrics#BUCKET_BOUNDS} for microseconds out to
   * 10s. Left aliased, that fix would have quietly made this default worse than what it replaced:
   * against bounds starting at 100, every distribution over small counts — spheres in a scene,
   * games in a month, rows in a drain — collapses into bucket 0, and its quantiles go with it.
   *
   * <p>Pinned as a non-equality because the failure it guards is someone re-collapsing the two back
   * into one constant, which reads like a tidy-up and passes every other test in this file.
   */
  @Test
  public void theUndeclaredDefaultIsNotTheHttpLatencyLayout() {
    assertThat(CustomMetrics.DEFAULT_BOUNDS)
        .as(
            "an undeclared distribution must not inherit the HTTP duration histogram's "
                + "microsecond bounds — those start at 100 and reach 10s, which buckets every "
                + "small-count instrument into slot 0")
        .isNotEqualTo(HttpServerMetrics.BUCKET_BOUNDS);
    assertThat(CustomMetrics.DEFAULT_BOUNDS[0])
        .as("the generic default has to start low enough to separate single-digit observations")
        .isLessThan(HttpServerMetrics.BUCKET_BOUNDS[0]);
  }

  /**
   * Every array that crosses this class's boundary is a copy, in both directions.
   *
   * <p>The interesting one is the last: an undeclared distribution buckets on {@link
   * CustomMetrics#DEFAULT_BOUNDS}, a shared static. If that reference reached a snapshot, one
   * caller editing what looks like its own result would rebucket every other undeclared
   * distribution in the process — in every {@code CustomMetrics} instance, since the array is
   * static — for the rest of the run. Nothing would throw; the exported {@code explicitBounds}
   * would simply stop describing the buckets beside it.
   */
  @Test
  public void boundsArraysAreCopiedOnEveryCrossing() {
    CustomMetrics metrics = new CustomMetrics();
    double[] declared = {10, 20, 30};
    metrics.defineDistribution("d", declared);

    declared[0] = 999; // the caller still holds the array it passed in
    assertThat(metrics.boundsFor("d")).containsExactly(10.0, 20.0, 30.0);

    metrics.boundsFor("d")[1] = 999; // and the array it was handed back
    assertThat(metrics.boundsFor("d")).containsExactly(10.0, 20.0, 30.0);

    metrics.record("d", 15);
    // And the one on its snapshot. Widening the *lowest* bound, not the highest: bucketing is
    // upper-inclusive first-match, so raising bounds[0] past a later observation pulls it all the
    // way down to bucket 0, while raising the top bound leaves everything where it was — which is
    // why the obvious edit here is a pin that passes against the aliased version too.
    metrics.distributionSnapshot().get(0).bounds()[0] = 100;
    metrics.record("d", 25);
    assertThat(metrics.distributionSnapshot().get(0).bucketCounts())
        .as("15 and 25 belong in buckets 1 and 2; against a live array the 25 drops to bucket 0")
        .containsExactly(0L, 1L, 1L, 0L);

    CustomMetrics other = new CustomMetrics();
    other.record("undeclared", 1);
    other.distributionSnapshot().get(0).bounds()[0] = 999;
    assertThat(CustomMetrics.DEFAULT_BOUNDS[0])
        .as("the shared default set is not reachable through a snapshot")
        .isNotEqualTo(999.0);
  }

  @Test
  public void distributionBoundsMustAscend() {
    CustomMetrics metrics = new CustomMetrics();
    assertThatThrownBy(() -> metrics.defineDistribution("d", new double[] {10, 5}))
        .isInstanceOf(IllegalArgumentException.class)
        .hasMessageContaining("ascend");
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
    // The placement, not just the total. Asserting "nothing overflowed" and "nothing was lost"
    // passes against a bucketFor that puts every value in bucket 0 — which leaves _sum and _count
    // right and every _bucket series and quantile wrong.
    // Bounds are {0,5,10,25,50,75,...}: 30 lands in the "<=50" slot, 70 in "<=75".
    int fifty = indexOfBound(50);
    int seventyFive = indexOfBound(75);
    assertThat(d.bucketCounts()[fifty]).as("30 belongs in <=50").isEqualTo(1L);
    assertThat(d.bucketCounts()[seventyFive]).as("70 belongs in <=75").isEqualTo(1L);
    assertThat(java.util.Arrays.stream(d.bucketCounts()).sum()).isEqualTo(2L);

    // Upper-inclusive: a value exactly on a bound belongs to that bucket, not the next.
    CustomMetrics onBoundary = new CustomMetrics();
    onBoundary.record("d", 50.0);
    assertThat(onBoundary.distributionSnapshot().get(0).bucketCounts()[fifty]).isEqualTo(1L);
  }

  private static int indexOfBound(double bound) {
    for (int i = 0; i < CustomMetrics.DEFAULT_BOUNDS.length; i++) {
      if (CustomMetrics.DEFAULT_BOUNDS[i] == bound) {
        return i;
      }
    }
    throw new IllegalArgumentException("no such bound: " + bound);
  }

  @Test
  public void anObservationBeyondTheLastBoundLandsInTheOverflowBucket() {
    CustomMetrics metrics = new CustomMetrics();
    metrics.record("month_duration_micros", 999_999.0);

    long[] buckets = metrics.distributionSnapshot().get(0).bucketCounts();
    assertThat(buckets[CustomMetrics.DEFAULT_BOUNDS.length])
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

  /**
   * The zero baseline, and the failure it exists to prevent: a counter whose first exported sample
   * already carries its first event's value shows no increase for that event, ever. increase() and
   * rate() measure between samples, so the event that created the series has nothing to be measured
   * against. Declaring the series up front gives it something.
   */
  @Test
  public void aDeclaredCounterExportsAtZeroBeforeAnythingHappens() {
    CustomMetrics metrics = new CustomMetrics();
    metrics.defineCounter("games_indexed");
    metrics.defineCounter("index_runs", Map.of("outcome", "completed"));

    assertThat(metrics.isEmpty())
        .as("a declared series must be exported, or there is no baseline to increase from")
        .isFalse();
    assertThat(metrics.counterSnapshot())
        .extracting(CustomMetrics.CounterSnapshot::name, CustomMetrics.CounterSnapshot::value)
        .containsExactlyInAnyOrder(
            org.assertj.core.groups.Tuple.tuple("games_indexed", 0L),
            org.assertj.core.groups.Tuple.tuple("index_runs", 0L));
  }

  /** The declared series is the same series the event lands on, not a second one beside it. */
  @Test
  public void aDeclaredCounterCountsNormallyOnceEventsArrive() {
    CustomMetrics metrics = new CustomMetrics();
    metrics.defineCounter("games_indexed");
    metrics.add("games_indexed", 75, Map.of());

    assertThat(metrics.counterSnapshot()).hasSize(1);
    assertThat(metrics.counterSnapshot().get(0).value()).isEqualTo(75L);
  }

  /**
   * Declaration runs at construction and events arrive later, but nothing stops a caller declaring
   * again — a re-declared counter must not lose what it has already counted.
   */
  @Test
  public void redeclaringACounterDoesNotResetIt() {
    CustomMetrics metrics = new CustomMetrics();
    metrics.increment("index_runs", Map.of("outcome", "completed"));
    metrics.defineCounter("index_runs", Map.of("outcome", "completed"));

    assertThat(metrics.counterSnapshot().get(0).value()).isEqualTo(1L);
  }

  /** Label sets are distinct series, so declaring one leaves the others to appear on first use. */
  @Test
  public void declaringOneLabelSetDoesNotDeclareItsSiblings() {
    CustomMetrics metrics = new CustomMetrics();
    metrics.defineCounter("index_runs", Map.of("outcome", "completed"));

    assertThat(metrics.counterSnapshot()).hasSize(1);
    assertThat(metrics.counterSnapshot().get(0).labels()).containsEntry("outcome", "completed");
  }

  /**
   * Distributions have the counter's problem one level down: defineDistribution declares bounds,
   * not a series, so the first observation still creates the series and a windowed mean over it
   * divides a one-sample rate by a one-sample rate.
   */
  @Test
  public void aDeclaredDistributionExportsEmptyBeforeAnyObservation() {
    CustomMetrics metrics = new CustomMetrics();
    metrics.defineDistribution("index_run_duration_micros", new double[] {1, 10, 100});
    metrics.defineDistributionSeries("index_run_duration_micros", Map.of("outcome", "completed"));

    assertThat(metrics.isEmpty()).isFalse();
    assertThat(metrics.distributionSnapshot()).hasSize(1);
    CustomMetrics.DistributionSnapshot snapshot = metrics.distributionSnapshot().get(0);
    assertThat(snapshot.count()).isZero();
    assertThat(snapshot.sum()).isZero();
    assertThat(snapshot.labels()).containsEntry("outcome", "completed");
    assertThat(snapshot.bounds())
        .as("a declared series must use the declared bounds, not the fallback")
        .containsExactly(1, 10, 100);
  }

  /** Declaring does not discard what a series has already observed. */
  @Test
  public void redeclaringADistributionSeriesKeepsItsObservations() {
    CustomMetrics metrics = new CustomMetrics();
    metrics.record("index_games_per_month", 40);
    metrics.defineDistributionSeries("index_games_per_month");

    assertThat(metrics.distributionSnapshot().get(0).count()).isEqualTo(1L);
    assertThat(metrics.distributionSnapshot().get(0).sum()).isEqualTo(40.0);
  }
}
