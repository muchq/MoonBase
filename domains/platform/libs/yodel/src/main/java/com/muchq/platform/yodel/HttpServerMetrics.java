package com.muchq.platform.yodel;

import java.util.ArrayList;
import java.util.Comparator;
import java.util.List;
import java.util.Map;
import java.util.concurrent.ConcurrentHashMap;
import java.util.concurrent.atomic.AtomicLong;
import java.util.concurrent.atomic.DoubleAdder;
import java.util.concurrent.atomic.LongAdder;

/**
 * The shared HTTP serving instruments (http_server_requests, http_server_requests_success /
 * _failure, http_server_requests_active_gauge, http_server_request_duration_microseconds) labeled
 * by service_name, http_method, and route, so prom_proxy can separate probe traffic from serving
 * traffic (https://github.com/muchq/MoonBase/issues/1303) while its standard block reads Java
 * services with zero changes (https://github.com/muchq/MoonBase/issues/1212). The label name
 * matches futility/otel; the value deliberately does not — futility emits the raw path, which is
 * unbounded, and this class's whole-template rule below is the corrected shape.
 *
 * <p>The route is the matched template ("/games/{id}"), never the raw path, so the label stays
 * bounded. It is only knowable once routing has happened, which shapes the recording contract: the
 * counters and the histogram move at completion (or abandonment), where the route is in hand; only
 * the in-flight gauge moves at request start, and it stays keyed by method alone. The requests
 * counter therefore counts completed-or-abandoned requests rather than started ones — the same
 * totals, observed a request-duration later. Once in-flight requests drain, requests equals success
 * + failure plus the abandoned count (which has no counter of its own: an abandoned request
 * increments requests and records no outcome); a snapshot taken mid-request can legitimately read
 * requests ahead of the outcomes.
 */
public final class HttpServerMetrics {
  // Microsecond-shaped bucket boundaries for the HTTP duration histogram,
  // running from 100µs out to 10s (#1286).
  //
  // These replace the OTel SDK defaults, which are shaped for milliseconds:
  // read as microseconds they topped out at 10ms, so every request slower than
  // that landed in +Inf and histogram_quantile answered a flat 10000 forever —
  // observed in production on portrait at a real p95 of ~1s (#1287).
  //
  // The C++ (futility/otel) and Rust (server_pal) emitters configure SDK views
  // with this same set, because prom_proxy's histogram_quantile query is shared
  // across all three languages and only compares like with like if the layouts
  // match. //domains/platform/libs/otel_contract pins them equal; if you change
  // this array, change the other two in the same commit or that test fails.
  //
  // Not retroactive: Prometheus keeps the old `le` series, so quantiles over a
  // window spanning the rollout read from both layouts and are wrong until the
  // old series age out.
  static final double[] BUCKET_BOUNDS = {
    100, 250, 500, 1000, 2500, 5000, 10000, 25000, 50000, 100000, 250000, 500000, 1000000, 2500000,
    10000000
  };

  private final String serviceName;
  private final long startTimeUnixNanos;
  private final ConcurrentHashMap<RouteKey, RouteStats> byRoute = new ConcurrentHashMap<>();
  private final ConcurrentHashMap<String, AtomicLong> activeByMethod = new ConcurrentHashMap<>();

  public HttpServerMetrics(String serviceName, long startTimeUnixNanos) {
    this.serviceName = serviceName;
    this.startTimeUnixNanos = startTimeUnixNanos;
  }

  public String serviceName() {
    return serviceName;
  }

  public long startTimeUnixNanos() {
    return startTimeUnixNanos;
  }

  /** Routing hasn't happened yet, so only the in-flight gauge can move here. */
  public void recordRequestStart(String httpMethod) {
    activeFor(httpMethod).incrementAndGet();
  }

  public void recordRequestComplete(
      String httpMethod, String route, int statusCode, double durationMicros) {
    activeFor(httpMethod).decrementAndGet();
    RouteStats stats = statsFor(httpMethod, route);
    stats.requests.increment();
    if (statusCode < 400) {
      stats.success.increment();
    } else {
      stats.failure.increment();
    }
    stats.durationSumMicros.add(durationMicros);
    stats.durationCount.increment();
    stats.bucketFor(durationMicros).increment();
  }

  /**
   * A request that ended without a response (the client went away first): it counts as a request —
   * it was real load — but records no outcome and no duration, matching server_pal's drop-guard
   * semantics.
   */
  public void recordRequestAbandoned(String httpMethod, String route) {
    activeFor(httpMethod).decrementAndGet();
    statsFor(httpMethod, route).requests.increment();
  }

  /** Point-in-time cumulative totals per (method, route), ordered for stable payloads. */
  public List<RouteSnapshot> snapshot() {
    List<RouteSnapshot> out = new ArrayList<>(byRoute.size());
    byRoute.entrySet().stream()
        .sorted(
            Comparator.comparing((Map.Entry<RouteKey, RouteStats> e) -> e.getKey().httpMethod())
                .thenComparing(e -> e.getKey().route()))
        .forEach(e -> out.add(e.getValue().snapshot(e.getKey())));
    return out;
  }

  /** Point-in-time in-flight counts per method, ordered by method for stable payloads. */
  public List<ActiveSnapshot> activeSnapshot() {
    List<ActiveSnapshot> out = new ArrayList<>(activeByMethod.size());
    activeByMethod.entrySet().stream()
        .sorted(Map.Entry.comparingByKey())
        .forEach(e -> out.add(new ActiveSnapshot(e.getKey(), e.getValue().get())));
    return out;
  }

  private RouteStats statsFor(String httpMethod, String route) {
    return byRoute.computeIfAbsent(new RouteKey(httpMethod, route), k -> new RouteStats());
  }

  private AtomicLong activeFor(String httpMethod) {
    return activeByMethod.computeIfAbsent(httpMethod, m -> new AtomicLong());
  }

  private record RouteKey(String httpMethod, String route) {}

  public record ActiveSnapshot(String httpMethod, long active) {}

  public record RouteSnapshot(
      String httpMethod,
      String route,
      long requests,
      long success,
      long failure,
      double durationSumMicros,
      long durationCount,
      long[] bucketCounts) {}

  private static final class RouteStats {
    final LongAdder requests = new LongAdder();
    final LongAdder success = new LongAdder();
    final LongAdder failure = new LongAdder();
    final DoubleAdder durationSumMicros = new DoubleAdder();
    final LongAdder durationCount = new LongAdder();
    final LongAdder[] buckets;

    RouteStats() {
      buckets = new LongAdder[BUCKET_BOUNDS.length + 1];
      for (int i = 0; i < buckets.length; i++) {
        buckets[i] = new LongAdder();
      }
    }

    // OTLP buckets are upper-inclusive: bounds[i-1] < x <= bounds[i].
    LongAdder bucketFor(double micros) {
      for (int i = 0; i < BUCKET_BOUNDS.length; i++) {
        if (micros <= BUCKET_BOUNDS[i]) {
          return buckets[i];
        }
      }
      return buckets[BUCKET_BOUNDS.length];
    }

    RouteSnapshot snapshot(RouteKey key) {
      long[] counts = new long[buckets.length];
      for (int i = 0; i < buckets.length; i++) {
        counts[i] = buckets[i].sum();
      }
      // Outcomes before requests, the reverse of the writer's order
      // (requests first, then the outcome): with the writer and reader
      // crossing in opposite directions, a torn read can only ever see
      // requests at or ahead of the outcomes — never success + failure
      // running past requests, which no request history can produce.
      double durationSum = durationSumMicros.sum();
      long durations = durationCount.sum();
      long succeeded = success.sum();
      long failed = failure.sum();
      long requested = requests.sum();
      return new RouteSnapshot(
          key.httpMethod(),
          key.route(),
          requested,
          succeeded,
          failed,
          durationSum,
          durations,
          counts);
    }
  }
}
