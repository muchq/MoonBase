package com.muchq.platform.yodel;

import static org.assertj.core.api.Assertions.assertThat;

import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.ObjectMapper;
import java.util.ArrayList;
import java.util.List;
import java.util.Map;
import org.junit.jupiter.api.Test;

/**
 * Pins the wire contract: instrument names, service_name/http_method/route labels, cumulative
 * temporality, and string-encoded 64-bit values, so the collector reads Java exactly like the C++
 * and Rust emitters (#1212, #1303).
 */
public class OtlpJsonEncoderTest {
  private static final ObjectMapper MAPPER = new ObjectMapper();

  private static JsonNode encode(HttpServerMetrics metrics) throws Exception {
    byte[] payload =
        OtlpJsonEncoder.encode(
            Map.of("service.name", "svc", "service.version", "abc123"), metrics, 2_000_000_000L);
    return MAPPER.readTree(payload);
  }

  @Test
  public void emitsTheFiveStandardInstruments() throws Exception {
    HttpServerMetrics metrics = new HttpServerMetrics("svc", 1_000_000_000L);
    metrics.recordRequestStart("GET");
    metrics.recordRequestComplete("GET", "/widgets", 200, 42.0);

    JsonNode metricNodes = encode(metrics).at("/resourceMetrics/0/scopeMetrics/0/metrics");

    List<String> names = new ArrayList<>();
    metricNodes.forEach(m -> names.add(m.get("name").asText()));
    assertThat(names)
        .containsExactly(
            "http_server_requests",
            "http_server_requests_success",
            "http_server_requests_failure",
            "http_server_requests_active_gauge",
            "http_server_request_duration_microseconds");
  }

  @Test
  public void sumsAreCumulativeWithProtoJsonStringInts() throws Exception {
    HttpServerMetrics metrics = new HttpServerMetrics("svc", 1_000_000_000L);
    metrics.recordRequestStart("GET");
    metrics.recordRequestComplete("GET", "/widgets", 200, 42.0);
    metrics.recordRequestStart("GET");
    metrics.recordRequestComplete("GET", "/widgets", 200, 7.0);

    JsonNode requests = encode(metrics).at("/resourceMetrics/0/scopeMetrics/0/metrics/0/sum");
    assertThat(requests.get("aggregationTemporality").asInt()).isEqualTo(2);
    assertThat(requests.get("isMonotonic").asBoolean()).isTrue();

    JsonNode point = requests.at("/dataPoints/0");
    assertThat(point.get("asInt").isTextual()).isTrue();
    assertThat(point.get("asInt").asText()).isEqualTo("2");
    assertThat(point.get("startTimeUnixNano").asText()).isEqualTo("1000000000");
    assertThat(point.get("timeUnixNano").asText()).isEqualTo("2000000000");
  }

  @Test
  public void activeGaugeIsANonMonotonicSum() throws Exception {
    HttpServerMetrics metrics = new HttpServerMetrics("svc", 1_000_000_000L);
    metrics.recordRequestStart("GET");

    JsonNode active = encode(metrics).at("/resourceMetrics/0/scopeMetrics/0/metrics/3");
    assertThat(active.get("name").asText()).isEqualTo("http_server_requests_active_gauge");
    assertThat(active.at("/sum/isMonotonic").asBoolean()).isFalse();
    assertThat(active.at("/sum/dataPoints/0/asInt").asText()).isEqualTo("1");
  }

  @Test
  public void histogramCarriesMicrosecondsWithDefaultBounds() throws Exception {
    HttpServerMetrics metrics = new HttpServerMetrics("svc", 1_000_000_000L);
    metrics.recordRequestStart("GET");
    metrics.recordRequestComplete("GET", "/widgets", 200, 42.0);

    JsonNode histogram =
        encode(metrics).at("/resourceMetrics/0/scopeMetrics/0/metrics/4/histogram");
    assertThat(histogram.get("aggregationTemporality").asInt()).isEqualTo(2);

    JsonNode point = histogram.at("/dataPoints/0");
    assertThat(point.get("count").asText()).isEqualTo("1");
    assertThat(point.get("sum").asDouble()).isEqualTo(42.0);
    assertThat(point.get("explicitBounds")).hasSize(HttpServerMetrics.BUCKET_BOUNDS.length);
    assertThat(point.get("bucketCounts")).hasSize(HttpServerMetrics.BUCKET_BOUNDS.length + 1);
    assertThat(point.at("/bucketCounts/0").isTextual()).isTrue();
  }

  /**
   * The #1303 guard: the request counter — and, through the shared sum() helper, the
   * success/failure counters — plus the histogram carry the route, so probe traffic is separable by
   * label. The route sits between yodel's historical http_method spelling and service_name, and is
   * spelled {@code route} to match futility.
   */
  @Test
  public void counterPointsCarryServiceNameMethodAndRouteLabels() throws Exception {
    HttpServerMetrics metrics = new HttpServerMetrics("svc", 1_000_000_000L);
    metrics.recordRequestStart("POST");
    metrics.recordRequestComplete("POST", "/health", 200, 5.0);

    JsonNode root = encode(metrics);
    JsonNode requestsAttributes =
        root.at("/resourceMetrics/0/scopeMetrics/0/metrics/0/sum/dataPoints/0/attributes");
    assertThat(attributeMap(requestsAttributes))
        .containsExactly(
            Map.entry("http_method", "POST"),
            Map.entry("route", "/health"),
            Map.entry("service_name", "svc"));
    JsonNode histogramAttributes =
        root.at("/resourceMetrics/0/scopeMetrics/0/metrics/4/histogram/dataPoints/0/attributes");
    assertThat(attributeMap(histogramAttributes)).containsKey("route");
  }

  /**
   * The gauge is the one instrument without a route: it moves at request start, before routing has
   * matched anything (#1303). A gauge point minted per route would also never return to zero per
   * key, which is exactly the unbounded-series shape the matched-template rule exists to prevent.
   */
  @Test
  public void gaugePointsCarryNoRouteLabel() throws Exception {
    HttpServerMetrics metrics = new HttpServerMetrics("svc", 1_000_000_000L);
    metrics.recordRequestStart("POST");

    JsonNode attributes =
        encode(metrics)
            .at("/resourceMetrics/0/scopeMetrics/0/metrics/3/sum/dataPoints/0/attributes");
    assertThat(attributeMap(attributes))
        .containsExactly(Map.entry("http_method", "POST"), Map.entry("service_name", "svc"));
  }

  @Test
  public void resourceCarriesTheOtelAttributes() throws Exception {
    HttpServerMetrics metrics = new HttpServerMetrics("svc", 1_000_000_000L);

    JsonNode attributes = encode(metrics).at("/resourceMetrics/0/resource/attributes");
    assertThat(attributeMap(attributes))
        .containsEntry("service.name", "svc")
        .containsEntry("service.version", "abc123");
  }

  private static Map<String, String> attributeMap(JsonNode attributes) {
    Map<String, String> out = new java.util.LinkedHashMap<>();
    attributes.forEach(a -> out.put(a.get("key").asText(), a.at("/value/stringValue").asText()));
    return out;
  }
}
