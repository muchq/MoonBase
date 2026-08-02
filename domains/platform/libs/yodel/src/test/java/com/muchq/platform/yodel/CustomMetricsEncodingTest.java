package com.muchq.platform.yodel;

import static org.assertj.core.api.Assertions.assertThat;

import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.ObjectMapper;
import java.util.ArrayList;
import java.util.List;
import java.util.Map;
import org.junit.jupiter.api.Test;

/**
 * Pins the wire contract for service-defined instruments, which is the half prom_proxy actually
 * queries. A counter that records correctly and encodes wrongly is invisible in exactly the way
 * that takes weeks to notice: the chart is empty, and an empty chart looks like an idle service.
 */
public class CustomMetricsEncodingTest {
  private static final ObjectMapper MAPPER = new ObjectMapper();

  private static JsonNode encode(CustomMetrics custom) throws Exception {
    byte[] payload =
        OtlpJsonEncoder.encode(
            Map.of("service.name", "svc"),
            new HttpServerMetrics("svc", 1_000_000_000L),
            custom,
            2_000_000_000L);
    return MAPPER.readTree(payload);
  }

  private static JsonNode customScope(JsonNode root) {
    for (JsonNode scope : root.at("/resourceMetrics/0/scopeMetrics")) {
      if ("custom".equals(scope.at("/scope/name").asText())) {
        return scope;
      }
    }
    return MAPPER.missingNode();
  }

  /**
   * Nothing recorded means no scope at all — not an empty one. Every 15 seconds forever is a long
   * time to ship a payload that says nothing.
   */
  @Test
  public void aServiceThatRecordsNothingEmitsNoCustomScope() throws Exception {
    JsonNode root = encode(new CustomMetrics());
    assertThat(root.at("/resourceMetrics/0/scopeMetrics")).hasSize(1);
    assertThat(customScope(root).isMissingNode()).isTrue();
  }

  @Test
  public void aCounterEncodesAsACumulativeMonotonicSum() throws Exception {
    CustomMetrics custom = new CustomMetrics();
    custom.add("games_indexed", 7, Map.of());

    JsonNode metric = customScope(encode(custom)).at("/metrics/0");
    assertThat(metric.get("name").asText()).isEqualTo("games_indexed");
    assertThat(metric.at("/sum/isMonotonic").asBoolean()).isTrue();
    assertThat(metric.at("/sum/aggregationTemporality").asInt()).isEqualTo(2);
    // proto3 JSON maps 64-bit ints to strings; a raw number silently loses precision at the top
    // of the range and some collectors reject it outright.
    assertThat(metric.at("/sum/dataPoints/0/asInt").isTextual()).isTrue();
    assertThat(metric.at("/sum/dataPoints/0/asInt").asText()).isEqualTo("7");
  }

  /**
   * One metric, many points — not many metrics sharing a name. The labels are dimensions of a
   * single series, and repeating the name instead is what makes a collector treat them as rivals.
   */
  @Test
  public void labelSetsBecomeDataPointsOfOneMetricRatherThanRepeatedMetrics() throws Exception {
    CustomMetrics custom = new CustomMetrics();
    custom.increment("index_runs", Map.of("outcome", "completed"));
    custom.increment("index_runs", Map.of("outcome", "failed"));
    custom.increment("index_runs", Map.of("outcome", "interrupted"));

    JsonNode metrics = customScope(encode(custom)).at("/metrics");
    List<String> names = new ArrayList<>();
    metrics.forEach(m -> names.add(m.get("name").asText()));
    assertThat(names).containsExactly("index_runs");
    assertThat(metrics.at("/0/sum/dataPoints")).hasSize(3);
  }

  /** prom_proxy scopes every query by service_name; a point without it belongs to no service. */
  @Test
  public void everyCustomPointCarriesServiceNameAlongsideItsOwnLabels() throws Exception {
    CustomMetrics custom = new CustomMetrics();
    custom.increment("index_runs", Map.of("outcome", "completed"));
    custom.record("index_run_duration_micros", 12.0, Map.of("outcome", "completed"));

    JsonNode scope = customScope(encode(custom));
    for (JsonNode metric : scope.at("/metrics")) {
      JsonNode points =
          metric.has("sum") ? metric.at("/sum/dataPoints") : metric.at("/histogram/dataPoints");
      for (JsonNode point : points) {
        Map<String, String> attrs = new java.util.HashMap<>();
        point
            .get("attributes")
            .forEach(a -> attrs.put(a.get("key").asText(), a.at("/value/stringValue").asText()));
        assertThat(attrs)
            .containsEntry("service_name", "svc")
            .containsEntry("outcome", "completed");
      }
    }
  }

  @Test
  public void aDistributionEncodesAsAHistogramWithTheSharedBounds() throws Exception {
    CustomMetrics custom = new CustomMetrics();
    custom.record("motif_occurrences_per_game", 3.0);
    custom.record("motif_occurrences_per_game", 8.0);

    JsonNode metric = customScope(encode(custom)).at("/metrics/0");
    assertThat(metric.get("name").asText()).isEqualTo("motif_occurrences_per_game");
    assertThat(metric.at("/histogram/aggregationTemporality").asInt()).isEqualTo(2);
    JsonNode point = metric.at("/histogram/dataPoints/0");
    assertThat(point.get("count").asText()).isEqualTo("2");
    assertThat(point.get("sum").asDouble()).isEqualTo(11.0);
    assertThat(point.get("explicitBounds")).hasSize(HttpServerMetrics.BUCKET_BOUNDS.length);
    // Buckets are bounds+1: the trailing one catches everything past the last bound.
    assertThat(point.get("bucketCounts")).hasSize(HttpServerMetrics.BUCKET_BOUNDS.length + 1);
  }

  /** The standard family must be untouched by any of this. */
  @Test
  public void theStandardHttpScopeStillShipsAlongsideTheCustomOne() throws Exception {
    CustomMetrics custom = new CustomMetrics();
    custom.increment("games_indexed");

    JsonNode root = encode(custom);
    assertThat(root.at("/resourceMetrics/0/scopeMetrics")).hasSize(2);
    assertThat(root.at("/resourceMetrics/0/scopeMetrics/0/scope/name").asText())
        .isEqualTo("http_server");
    assertThat(customScope(root).isMissingNode()).isFalse();
  }
}
