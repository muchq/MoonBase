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
    metrics.recordRequestComplete("GET", 200, 5.0); // on the boundary: bucket for <=5
    metrics.recordRequestStart("GET");
    metrics.recordRequestComplete("GET", 200, 6.0); // bucket for <=10
    metrics.recordRequestStart("GET");
    metrics.recordRequestComplete("GET", 200, 20_000.0); // overflow bucket

    long[] buckets = metrics.snapshot().get(0).bucketCounts();
    assertThat(buckets).hasSize(HttpServerMetrics.BUCKET_BOUNDS.length + 1);
    assertThat(buckets[1]).isEqualTo(1); // (0, 5]
    assertThat(buckets[2]).isEqualTo(1); // (5, 10]
    assertThat(buckets[buckets.length - 1]).isEqualTo(1); // (10000, +Inf)
  }

  /**
   * The HTTP bounds are wrong and may not be fixed here alone (#1286).
   *
   * <p>They are the OTel SDK defaults, which are shaped for milliseconds; this emitter records
   * microseconds, so the top bound is 10ms and every p95 tile is capped there. The Rust
   * (server_pal) and C++ (futility/otel) emitters inherit the same defaults from their SDKs and
   * record microseconds too, and prom_proxy charts all three through one shared PromQL expression.
   * Widening Java's array on its own would leave those services answering the same query on a
   * different bucket layout — a worse state than the one it fixes, and a silent one.
   *
   * <p>So this test does not assert the bounds are right. It asserts they still match the defaults
   * the other two emitters get, and fails the moment Java drifts alone.
   */
  @Test
  public void httpBoundsStayMatchedToTheOtherEmittersUntilAllThreeMove() {
    double[] otelSdkDefaults = {
      0, 5, 10, 25, 50, 75, 100, 250, 500, 750, 1000, 2500, 5000, 7500, 10000
    };

    assertThat(HttpServerMetrics.BUCKET_BOUNDS)
        .as(
            "yodel's HTTP bounds no longer match the OTel SDK defaults that server_pal (Rust) and "
                + "futility/otel (C++) inherit. If this is the #1286 fix, it has to land in all "
                + "three emitters together — prom_proxy's p95 query spans them. If it is not, "
                + "Java services have quietly become incomparable to every other service.")
        .isEqualTo(otelSdkDefaults);
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
