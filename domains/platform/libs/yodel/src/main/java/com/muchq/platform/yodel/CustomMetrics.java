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
  private final ConcurrentHashMap<String, double[]> boundsByName = new ConcurrentHashMap<>();

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

  /**
   * Declares the bucket bounds for a distribution, ahead of the first observation.
   *
   * <p>The default is the set the HTTP latency histogram uses, which tops out at 10ms. That suits
   * anything request-shaped and suits nothing else: a distribution over minutes, or over counts in
   * the thousands, puts every observation in the overflow bucket. The mean still reads correctly
   * off {@code _sum}/{@code _count}, so the damage is quiet — fifteen bucket series pinned at zero,
   * and any quantile over them returning {@code +Inf}.
   *
   * <p>Call before recording. Bounds must be ascending, and a later call for the same name is
   * ignored rather than allowed to reshape a histogram mid-flight — the collector treats bucket
   * counts as cumulative, and swapping bounds underneath them corrupts the series rather than
   * correcting it.
   */
  public void defineDistribution(String name, double[] bounds) {
    requireValid("metric name", name);
    if (bounds.length == 0) {
      throw new IllegalArgumentException("distribution " + name + " needs at least one bound");
    }
    for (int i = 1; i < bounds.length; i++) {
      if (bounds[i] <= bounds[i - 1]) {
        throw new IllegalArgumentException(
            "distribution "
                + name
                + " bounds must ascend, got "
                + bounds[i - 1]
                + " then "
                + bounds[i]);
      }
    }
    boundsByName.putIfAbsent(name, bounds.clone());
  }

  /** The bounds a distribution will use, whether declared or defaulted. */
  public double[] boundsFor(String name) {
    return boundsByName.getOrDefault(name, HttpServerMetrics.BUCKET_BOUNDS).clone();
  }

  /** Records one observation of {@code name} with no labels. */
  public void record(String name, double value) {
    record(name, value, Map.of());
  }

  /**
   * Records one observation of {@code name} for this label set.
   *
   * <p>Rejects NaN and negatives for the same reason {@link #add} rejects a negative delta, and the
   * failure is quieter here: the exported {@code _sum} is cumulative, so a negative observation
   * makes it fall and Prometheus reads the drop as a counter reset and invents a {@code rate()}
   * spike. NaN is worse — {@code DoubleAdder} keeps it forever, and protojson accepts the string
   * {@code "NaN"} for a double, so the series parses cleanly and is silently NaN from then on.
   */
  public void record(String name, double value, Map<String, String> labels) {
    if (Double.isNaN(value) || Double.isInfinite(value)) {
      throw new IllegalArgumentException("distribution " + name + " cannot record " + value);
    }
    if (value < 0) {
      throw new IllegalArgumentException(
          "distribution " + name + " cannot record a negative observation (" + value + ")");
    }
    distributions
        .computeIfAbsent(
            series(name, labels),
            s -> new Distribution(boundsByName.getOrDefault(name, HttpServerMetrics.BUCKET_BOUNDS)))
        .record(value);
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
                        e.getKey().name(),
                        // Copy: the key's own map decides its hash. Handing it out lets a caller
                        // mutate a live key, stranding the entry in the wrong bin so the next
                        // record for those labels mints a second one — two data points with
                        // identical attributes in one payload, which Prometheus rejects.
                        Map.copyOf(e.getKey().labels()),
                        e.getValue().sum())));
    return out;
  }

  /** Cumulative distribution state, ordered by name then labels so payloads are stable. */
  public List<DistributionSnapshot> distributionSnapshot() {
    List<DistributionSnapshot> out = new ArrayList<>(distributions.size());
    distributions.entrySet().stream()
        .sorted(Map.Entry.comparingByKey())
        .forEach(
            e ->
                out.add(e.getValue().snapshot(e.getKey().name(), Map.copyOf(e.getKey().labels()))));
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
          // The encoder stamps service_name on every point, and the OTel spec forbids a data point
          // carrying the same attribute key twice. Refusing it here beats emitting a payload a
          // strict collector drops.
          if ("service_name".equals(key)) {
            throw new IllegalArgumentException(
                "service_name is added on export; a metric may not set it as a label");
          }
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
    // Compared entry by entry rather than on the map's toString: "{a=1, b=2}" is also what a
    // single entry {a="1, b=2"} stringifies to, and label values are not validated. Two distinct
    // series comparing equal would leave their order to hash iteration, which is the opposite of
    // the stable payload this ordering exists to produce.
    private static final Comparator<Series> ORDER =
        Comparator.comparing(Series::name)
            .thenComparing(s -> s.labels().size())
            .thenComparing(s -> String.join("\u0000", s.labels().keySet()))
            .thenComparing(s -> String.join("\u0000", s.labels().values()));

    @Override
    public int compareTo(Series other) {
      return ORDER.compare(this, other);
    }
  }

  public record CounterSnapshot(String name, Map<String, String> labels, long value) {}

  public record DistributionSnapshot(
      String name,
      Map<String, String> labels,
      double sum,
      long count,
      long[] bucketCounts,
      double[] bounds) {}

  private static final class Distribution {
    final DoubleAdder sum = new DoubleAdder();
    final LongAdder count = new LongAdder();
    final double[] bounds;
    final LongAdder[] buckets;

    Distribution(double[] bounds) {
      this.bounds = bounds;
      buckets = new LongAdder[bounds.length + 1];
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
      for (int i = 0; i < bounds.length; i++) {
        if (value <= bounds[i]) {
          return buckets[i];
        }
      }
      return buckets[bounds.length];
    }

    DistributionSnapshot snapshot(String name, Map<String, String> labels) {
      long[] counts = new long[buckets.length];
      for (int i = 0; i < buckets.length; i++) {
        counts[i] = buckets[i].sum();
      }
      return new DistributionSnapshot(name, labels, sum.sum(), count.sum(), counts, bounds);
    }
  }
}
