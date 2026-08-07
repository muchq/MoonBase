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
 * (https://github.com/muchq/MoonBase/issues/1212). The C++ rail already had the equivalent —
 * futility/otel's {@code RecordCounter} and {@code RecordDistribution} — so this is Java catching
 * up to it rather than a new idea. The Rust rail has not caught up: server_pal exposes only the
 * HTTP family, which is why mithril and posterize are still empty entries in that same registry.
 *
 * <p>Two instrument kinds, matching what the dashboard can already render:
 *
 * <ul>
 *   <li><b>Counters</b> — monotonic totals. The collector's Prometheus exporter appends {@code
 *       _total}, so a counter named {@code games_indexed} is queried as {@code
 *       games_indexed_total}.
 *   <li><b>Distributions</b> — histograms exported as {@code _sum}/{@code _count}/{@code _bucket},
 *       over bounds the instrument declares for itself via {@link #defineDistribution}. A windowed
 *       mean is then {@code rate(x_sum[5m])/rate(x_count[5m])}, which is how portrait — a C++
 *       service, on futility/otel — reports scene complexity.
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

  /**
   * Bounds for a distribution that never declared any: the OTel SDK defaults, spanning 0 to 10000.
   *
   * <p>Deliberately <em>not</em> {@link HttpServerMetrics#BUCKET_BOUNDS}. It used to be, back when
   * that array also held the SDK defaults. #1286 reshaped it for microsecond latency out to 10s,
   * which is the right layout for request durations and the wrong one for everything else — against
   * those bounds a distribution over small counts puts every observation in the first bucket, which
   * is a worse default than what it replaced. Latency is one instrument's concern; this is the
   * fallback for arbitrary ones, so the two moved apart.
   *
   * <p>Still only a fallback, and still not a good one for most instruments — see {@link
   * #defineDistribution}.
   */
  static final double[] DEFAULT_BOUNDS = {
    0, 5, 10, 25, 50, 75, 100, 250, 500, 750, 1000, 2500, 5000, 7500, 10000
  };

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
   * Declares a counter series ahead of its first event, so it exports as 0 from process start.
   *
   * <p>Without this a counter springs into existence already carrying the value of the first event
   * it counted, and that first event is then invisible forever. {@code increase()} and {@code
   * rate()} measure the change <em>between</em> samples, so they need an earlier sample to measure
   * from; a series whose very first sample is 75 shows no increase at all, and every later sample
   * is flat until something else happens. The counter is not wrong — {@code games_indexed_total}
   * really does read 75 — but every dashboard built on it reads zero, which is worse than a missing
   * tile because it looks like an answer.
   *
   * <p>That is not a corner case: it is what every process does after every deploy, so the first
   * run of anything is uncounted until a second one follows it. It cost a real investigation on
   * one_d4, where an overnight indexing run of a thousand games left an Indexing panel of zeros
   * (https://github.com/muchq/MoonBase/issues/1323).
   *
   * <p>Declare the label sets whose values are known up front — outcomes, results, states. An
   * unbounded label (a username, a URL) has no series to declare, and declaring one per possible
   * value is how a metric becomes a cardinality problem; leave those to appear on first use and
   * accept that their first event is the baseline.
   *
   * <p>Idempotent, and never resets a counter that has already counted something: a repeat call for
   * the same series leaves the existing total alone.
   */
  public void defineCounter(String name, Map<String, String> labels) {
    counters.computeIfAbsent(series(name, labels), s -> new LongAdder());
  }

  /** Declares an unlabelled counter series. See {@link #defineCounter(String, Map)}. */
  public void defineCounter(String name) {
    defineCounter(name, Map.of());
  }

  /**
   * Declares the bucket bounds for a distribution, ahead of the first observation.
   *
   * <p>The default is {@link #DEFAULT_BOUNDS}, which tops out at 10000 and is a guess about no
   * instrument in particular. A distribution over minutes, or over counts in the millions, puts
   * every observation in the overflow bucket; one over single-digit counts puts every observation
   * in the first two. Declare bounds for anything whose range you know — which is nearly everything
   * — and treat inheriting this default as a bug rather than a choice.
   *
   * <p>The damage from getting it wrong is quiet, which is what makes it worth saying twice. The
   * mean still reads correctly off {@code _sum}/{@code _count}, so the tile beside the broken one
   * stays right. What breaks is every quantile: {@code histogram_quantile} falls back to the
   * highest finite bucket when the rank lands in {@code +Inf}, so the chart answers the top bound
   * forever rather than showing nothing. That is exactly how #1286 hid in the HTTP histogram until
   * someone put real traffic through it (#1287).
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
    return boundsByName.getOrDefault(name, DEFAULT_BOUNDS).clone();
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
            s -> new Distribution(boundsByName.getOrDefault(name, DEFAULT_BOUNDS)))
        .record(value);
  }

  /**
   * Declares a distribution series ahead of its first observation, so it exports as an empty
   * histogram — count 0, sum 0, every bucket 0 — from process start.
   *
   * <p>Same failure as {@link #defineCounter}, one level less obvious. {@link #defineDistribution}
   * declares only the <em>bounds</em>; the series itself is still created by the first {@code
   * record}, so a windowed mean of {@code rate(x_sum)/rate(x_count)} over that first observation
   * divides a rate computed from one sample by another, and the first run's duration never appears
   * on the chart it was instrumented for.
   *
   * <p>Takes the label set, which is why it cannot simply be folded into {@code
   * defineDistribution}: bounds are per name, but a series is per name <em>and</em> labels, and
   * declaring bounds for {@code index_run_duration_micros} says nothing about which outcomes it
   * will be sliced by.
   *
   * <p>Idempotent, and never discards observations a series has already taken.
   */
  public void defineDistributionSeries(String name, Map<String, String> labels) {
    distributions.computeIfAbsent(
        series(name, labels),
        s -> new Distribution(boundsByName.getOrDefault(name, DEFAULT_BOUNDS)));
  }

  /** Declares an unlabelled distribution series. See {@link #defineDistributionSeries}. */
  public void defineDistributionSeries(String name) {
    defineDistributionSeries(name, Map.of());
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
      // Held by reference, deliberately: for an undeclared distribution this is the shared
      // DEFAULT_BOUNDS static, and for a declared one it is the array boundsByName already
      // owns. Neither escapes — boundsFor and snapshot both copy — and a clone here would be a
      // third copy no test can distinguish from its absence.
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
      // bounds is cloned for the same reason the label map is copied above: handing out the live
      // array lets a caller reshape a histogram that is already recording into it, so observations
      // before and after the edit sit in buckets that mean different things while the exported
      // explicitBounds claims one set. A thirteen-element copy per series per export is nothing
      // beside the buckets we already allocate on the same line.
      return new DistributionSnapshot(name, labels, sum.sum(), count.sum(), counts, bounds.clone());
    }
  }
}
