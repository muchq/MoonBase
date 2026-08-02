package com.muchq.platform.yodel;

import java.util.ArrayList;
import java.util.List;
import java.util.Map;
import java.util.concurrent.ConcurrentHashMap;
import java.util.concurrent.atomic.AtomicLong;
import java.util.concurrent.atomic.DoubleAdder;
import java.util.concurrent.atomic.LongAdder;

/**
 * The shared HTTP serving instruments (http_server_requests, http_server_requests_success /
 * _failure, http_server_requests_active_gauge, http_server_request_duration_microseconds) labeled
 * by service_name and http_method, mirroring futility/otel (C++) and server_pal (Rust) so
 * prom_proxy's standard block reads Java services with zero changes
 * (https://github.com/muchq/MoonBase/issues/1212).
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
  private final ConcurrentHashMap<String, MethodStats> byMethod = new ConcurrentHashMap<>();

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

  public void recordRequestStart(String httpMethod) {
    MethodStats stats = statsFor(httpMethod);
    stats.requests.increment();
    stats.active.incrementAndGet();
  }

  public void recordRequestComplete(String httpMethod, int statusCode, double durationMicros) {
    MethodStats stats = statsFor(httpMethod);
    stats.active.decrementAndGet();
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
   * A request that ended without a response (the client went away first): only the in-flight gauge
   * moves, matching server_pal's drop-guard semantics.
   */
  public void recordRequestAbandoned(String httpMethod) {
    statsFor(httpMethod).active.decrementAndGet();
  }

  /** Point-in-time cumulative totals per method, ordered by method for stable payloads. */
  public List<MethodSnapshot> snapshot() {
    List<MethodSnapshot> out = new ArrayList<>(byMethod.size());
    byMethod.entrySet().stream()
        .sorted(Map.Entry.comparingByKey())
        .forEach(e -> out.add(e.getValue().snapshot(e.getKey())));
    return out;
  }

  private MethodStats statsFor(String httpMethod) {
    return byMethod.computeIfAbsent(httpMethod, m -> new MethodStats());
  }

  public record MethodSnapshot(
      String httpMethod,
      long requests,
      long success,
      long failure,
      long active,
      double durationSumMicros,
      long durationCount,
      long[] bucketCounts) {}

  private static final class MethodStats {
    final LongAdder requests = new LongAdder();
    final LongAdder success = new LongAdder();
    final LongAdder failure = new LongAdder();
    final AtomicLong active = new AtomicLong();
    final DoubleAdder durationSumMicros = new DoubleAdder();
    final LongAdder durationCount = new LongAdder();
    final LongAdder[] buckets;

    MethodStats() {
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

    MethodSnapshot snapshot(String httpMethod) {
      long[] counts = new long[buckets.length];
      for (int i = 0; i < buckets.length; i++) {
        counts[i] = buckets[i].sum();
      }
      return new MethodSnapshot(
          httpMethod,
          requests.sum(),
          success.sum(),
          failure.sum(),
          active.get(),
          durationSumMicros.sum(),
          durationCount.sum(),
          counts);
    }
  }
}
