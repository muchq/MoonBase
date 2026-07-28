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
