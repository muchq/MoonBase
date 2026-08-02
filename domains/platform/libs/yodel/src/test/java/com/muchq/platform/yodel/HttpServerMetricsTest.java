package com.muchq.platform.yodel;

import static org.assertj.core.api.Assertions.assertThat;

import java.util.List;
import org.junit.jupiter.api.Test;

public class HttpServerMetricsTest {

  @Test
  public void startAndCompleteKeepCountersAndGaugeSymmetric() {
    HttpServerMetrics metrics = new HttpServerMetrics("svc", 0L);

    metrics.recordRequestStart("GET");
    List<HttpServerMetrics.MethodSnapshot> inFlight = metrics.snapshot();
    assertThat(inFlight).hasSize(1);
    assertThat(inFlight.get(0).requests()).isEqualTo(1);
    assertThat(inFlight.get(0).active()).isEqualTo(1);
    assertThat(inFlight.get(0).durationCount()).isZero();

    metrics.recordRequestComplete("GET", 200, 42.0);
    HttpServerMetrics.MethodSnapshot done = metrics.snapshot().get(0);
    assertThat(done.requests()).isEqualTo(1);
    assertThat(done.active()).isZero();
    assertThat(done.success()).isEqualTo(1);
    assertThat(done.failure()).isZero();
    assertThat(done.durationCount()).isEqualTo(1);
    assertThat(done.durationSumMicros()).isEqualTo(42.0);
  }

  @Test
  public void statusSplitsAtFourHundred() {
    HttpServerMetrics metrics = new HttpServerMetrics("svc", 0L);
    metrics.recordRequestStart("GET");
    metrics.recordRequestComplete("GET", 399, 1.0);
    metrics.recordRequestStart("GET");
    metrics.recordRequestComplete("GET", 400, 1.0);
    metrics.recordRequestStart("GET");
    metrics.recordRequestComplete("GET", 500, 1.0);

    HttpServerMetrics.MethodSnapshot snapshot = metrics.snapshot().get(0);
    assertThat(snapshot.success()).isEqualTo(1);
    assertThat(snapshot.failure()).isEqualTo(2);
  }

  @Test
  public void abandonedRequestsOnlyMoveTheGauge() {
    HttpServerMetrics metrics = new HttpServerMetrics("svc", 0L);
    metrics.recordRequestStart("POST");
    metrics.recordRequestAbandoned("POST");

    HttpServerMetrics.MethodSnapshot snapshot = metrics.snapshot().get(0);
    assertThat(snapshot.requests()).isEqualTo(1);
    assertThat(snapshot.active()).isZero();
    assertThat(snapshot.success()).isZero();
    assertThat(snapshot.failure()).isZero();
    assertThat(snapshot.durationCount()).isZero();
  }

  @Test
  public void durationsLandInUpperInclusiveBuckets() {
    HttpServerMetrics metrics = new HttpServerMetrics("svc", 0L);
    metrics.recordRequestStart("GET");
    metrics.recordRequestComplete("GET", 200, 100.0); // on the boundary: bucket for <=100
    metrics.recordRequestStart("GET");
    metrics.recordRequestComplete("GET", 200, 101.0); // bucket for <=250
    metrics.recordRequestStart("GET");
    metrics.recordRequestComplete("GET", 200, 20_000_000.0); // 20s: overflow bucket

    long[] buckets = metrics.snapshot().get(0).bucketCounts();
    assertThat(buckets).hasSize(HttpServerMetrics.BUCKET_BOUNDS.length + 1);
    assertThat(buckets[0]).isEqualTo(1); // [0, 100]
    assertThat(buckets[1]).isEqualTo(1); // (100, 250]
    assertThat(buckets[buckets.length - 1]).isEqualTo(1); // (10000000, +Inf)
  }

  /**
   * The #1286 regression, replayed as the traffic that exposed it (#1287).
   *
   * <p>Against the old millisecond-shaped defaults the top finite bound was 10000 — 10ms, read as
   * microseconds — so both batches below landed in {@code +Inf} together. {@code
   * histogram_quantile} answers the highest finite bound when the rank falls in the overflow
   * bucket, so p95 read a flat 10000 whether the service was serving in 8ms or in a second. That is
   * the whole defect: not a chart that breaks, a chart that lies and holds steady while doing it.
   *
   * <p>Asserted as separation rather than as placement. Pinning "the 1s requests are in bucket 12"
   * passes against any layout that happens to have twelve slots below a second; what has to be true
   * is that the fast batch and the slow batch land in <em>different, finite</em> buckets, because
   * that is the only property a quantile can read.
   */
  @Test
  public void aSecondLongRequestNoLongerFallsOffTheTopOfTheHistogram() {
    HttpServerMetrics metrics = new HttpServerMetrics("portrait", 0L);
    // The two batches from #1287: 11 light requests around 8.6ms, 5 heavy ones around 1s.
    for (int i = 0; i < 11; i++) {
      metrics.recordRequestStart("POST");
      metrics.recordRequestComplete("POST", 200, 8_669.5);
    }
    for (int i = 0; i < 5; i++) {
      metrics.recordRequestStart("POST");
      metrics.recordRequestComplete("POST", 200, 1_000_000.0);
    }

    long[] buckets = metrics.snapshot().get(0).bucketCounts();
    int light = bucketIndexFor(8_669.5);
    int heavy = bucketIndexFor(1_000_000.0);

    assertThat(heavy)
        .as("a 1s request must land in a finite bucket, not the overflow slot p95 collapses onto")
        .isLessThan(HttpServerMetrics.BUCKET_BOUNDS.length);
    assertThat(light)
        .as("8.6ms and 1s have to be distinguishable, or no quantile between them can be right")
        .isLessThan(heavy);
    assertThat(buckets[light]).isEqualTo(11);
    assertThat(buckets[heavy]).isEqualTo(5);
    assertThat(buckets[buckets.length - 1])
        .as("nothing in a realistic HTTP workload belongs past the top bound")
        .isZero();
  }

  /** Upper-inclusive first match, the same rule {@code HttpServerMetrics} buckets by. */
  private static int bucketIndexFor(double micros) {
    for (int i = 0; i < HttpServerMetrics.BUCKET_BOUNDS.length; i++) {
      if (micros <= HttpServerMetrics.BUCKET_BOUNDS[i]) {
        return i;
      }
    }
    return HttpServerMetrics.BUCKET_BOUNDS.length;
  }

  @Test
  public void snapshotOrdersMethodsDeterministically() {
    HttpServerMetrics metrics = new HttpServerMetrics("svc", 0L);
    metrics.recordRequestStart("POST");
    metrics.recordRequestStart("DELETE");
    metrics.recordRequestStart("GET");

    assertThat(metrics.snapshot())
        .extracting(HttpServerMetrics.MethodSnapshot::httpMethod)
        .containsExactly("DELETE", "GET", "POST");
  }
}
