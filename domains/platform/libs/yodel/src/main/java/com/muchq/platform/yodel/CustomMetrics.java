package com.muchq.platform.yodel;

import java.util.ArrayList;
import java.util.Comparator;
import java.util.List;
import java.util.Map;
import java.util.SortedMap;
import java.util.TreeMap;
import java.util.concurrent.ConcurrentHashMap;
import java.util.concurrent.atomic.DoubleAdder;
import java.util.concurrent.atomic.LongAdder;
import java.util.regex.Pattern;

/**
 * Instruments a service defines for itself, exported beside the {@code http_server_*} family.
 *
 * <p>Until this existed a Java service could say how many requests it served and nothing about what
 * it did with them, which is why every Java entry in prom_proxy's registry was empty
 * (https://github.com/muchq/MoonBase/issues/1212). The C++ and Rust rails already had the
 * equivalent — futility/otel's counters and server_pal's {@code RecordDistribution} — so this is
 * Java catching up to them rather than a new idea.
 *
 * <p>Two instrument kinds, matching what the dashboard can already render:
 *
 * <ul>
 *   <li><b>Counters</b> — monotonic totals. The collector's Prometheus exporter appends {@code
 *       _total}, so a counter named {@code games_indexed} is queried as {@code
 *       games_indexed_total}.
 *   <li><b>Distributions</b> — histograms over the same bucket bounds the HTTP duration histogram
 *       uses, exported as {@code _sum}/{@code _count}/{@code _bucket}. A windowed mean is then
 *       {@code rate(x_sum[5m])/rate(x_count[5m])}, which is how portrait reports scene complexity.
 * </ul>
 *
 * <p>Labels are for bounded, low-cardinality dimensions — an outcome, a stage, a motif name. Never
 * a user id, a player name, or a URL: every distinct label set is a separate stored series, and one
 * unbounded label is what turns a metrics backend into an outage.
 *
 * <p>Safe to call from any thread; recording is lock-free.
 */
public final class CustomMetrics {

  /**
   * Prometheus' own name grammar. Enforced on the way in because the alternative is a metric that
   * records perfectly, exports, and then quietly fails to match any dashboard query — a failure
   * that shows up as an empty chart weeks later rather than as anything a test would catch.
   */
  private static final Pattern VALID_NAME = Pattern.compile("[a-zA-Z_][a-zA-Z0-9_]*");

  private final ConcurrentHashMap<Series, LongAdder> counters = new ConcurrentHashMap<>();
  private final ConcurrentHashMap<Series, Distribution> distributions = new ConcurrentHashMap<>();

  /** Adds one to {@code name} with no labels. */
  public void increment(String name) {
    add(name, 1, Map.of());
  }

  /** Adds one to {@code name} for this label set. */
  public void increment(String name, Map<String, String> labels) {
    add(name, 1, labels);
  }

  /** Adds {@code delta} to {@code name} for this label set. Counters only go up. */
  public void add(String name, long delta, Map<String, String> labels) {
    if (delta < 0) {
      throw new IllegalArgumentException(
          "counter " + name + " may not decrease (delta " + delta + ")");
    }
    counters.computeIfAbsent(series(name, labels), s -> new LongAdder()).add(delta);
  }

  /** Records one observation of {@code name} with no labels. */
  public void record(String name, double value) {
    record(name, value, Map.of());
  }

  /** Records one observation of {@code name} for this label set. */
  public void record(String name, double value, Map<String, String> labels) {
    distributions.computeIfAbsent(series(name, labels), s -> new Distribution()).record(value);
  }

  /** Cumulative counter totals, ordered by name then labels so payloads are stable. */
  public List<CounterSnapshot> counterSnapshot() {
    List<CounterSnapshot> out = new ArrayList<>(counters.size());
    counters.entrySet().stream()
        .sorted(Map.Entry.comparingByKey())
        .forEach(
            e ->
                out.add(
                    new CounterSnapshot(
                        e.getKey().name(), e.getKey().labels(), e.getValue().sum())));
    return out;
  }

  /** Cumulative distribution state, ordered by name then labels so payloads are stable. */
  public List<DistributionSnapshot> distributionSnapshot() {
    List<DistributionSnapshot> out = new ArrayList<>(distributions.size());
    distributions.entrySet().stream()
        .sorted(Map.Entry.comparingByKey())
        .forEach(e -> out.add(e.getValue().snapshot(e.getKey().name(), e.getKey().labels())));
    return out;
  }

  /** True when nothing has been recorded, so the encoder can leave the scope out entirely. */
  public boolean isEmpty() {
    return counters.isEmpty() && distributions.isEmpty();
  }

  private static Series series(String name, Map<String, String> labels) {
    requireValid("metric name", name);
    SortedMap<String, String> sorted = new TreeMap<>();
    labels.forEach(
        (key, value) -> {
          requireValid("label name", key);
          sorted.put(key, value);
        });
    return new Series(name, sorted);
  }

  private static void requireValid(String what, String value) {
    if (value == null || !VALID_NAME.matcher(value).matches()) {
      throw new IllegalArgumentException(
          what + " must match " + VALID_NAME.pattern() + " to survive export, got: " + value);
    }
  }

  /**
   * A single exported series. Labels are held sorted so that two callers passing the same pairs in
   * different orders land on one series rather than two that silently double-count.
   */
  record Series(String name, SortedMap<String, String> labels) implements Comparable<Series> {
    private static final Comparator<Series> ORDER =
        Comparator.comparing(Series::name).thenComparing(s -> s.labels().toString());

    @Override
    public int compareTo(Series other) {
      return ORDER.compare(this, other);
    }
  }

  public record CounterSnapshot(String name, Map<String, String> labels, long value) {}

  public record DistributionSnapshot(
      String name, Map<String, String> labels, double sum, long count, long[] bucketCounts) {}

  private static final class Distribution {
    final DoubleAdder sum = new DoubleAdder();
    final LongAdder count = new LongAdder();
    final LongAdder[] buckets;

    Distribution() {
      buckets = new LongAdder[HttpServerMetrics.BUCKET_BOUNDS.length + 1];
      for (int i = 0; i < buckets.length; i++) {
        buckets[i] = new LongAdder();
      }
    }

    void record(double value) {
      sum.add(value);
      count.increment();
      bucketFor(value).increment();
    }

    // Upper-inclusive, matching HttpServerMetrics and the OTLP bucket convention.
    LongAdder bucketFor(double value) {
      for (int i = 0; i < HttpServerMetrics.BUCKET_BOUNDS.length; i++) {
        if (value <= HttpServerMetrics.BUCKET_BOUNDS[i]) {
          return buckets[i];
        }
      }
      return buckets[HttpServerMetrics.BUCKET_BOUNDS.length];
    }

    DistributionSnapshot snapshot(String name, Map<String, String> labels) {
      long[] counts = new long[buckets.length];
      for (int i = 0; i < buckets.length; i++) {
        counts[i] = buckets[i].sum();
      }
      return new DistributionSnapshot(name, labels, sum.sum(), count.sum(), counts);
    }
  }
}
