package com.muchq.platform.yodel;

import com.fasterxml.jackson.core.JsonProcessingException;
import com.fasterxml.jackson.databind.ObjectMapper;
import com.fasterxml.jackson.databind.node.ArrayNode;
import com.fasterxml.jackson.databind.node.ObjectNode;
import java.util.List;
import java.util.Map;
import java.util.function.ToLongFunction;

/**
 * Renders the http_server_* family as an OTLP/HTTP JSON ExportMetricsServiceRequest. 64-bit values
 * are encoded as strings per the proto3 JSON mapping, temporality is cumulative, and instrument
 * names match futility/otel and server_pal exactly — the collector's Prometheus exporter turns them
 * into http_server_requests_total, http_server_requests_active_gauge, and
 * http_server_request_duration_microseconds_{sum,count,bucket}.
 */
final class OtlpJsonEncoder {
  private static final ObjectMapper MAPPER = new ObjectMapper();
  private static final int AGGREGATION_TEMPORALITY_CUMULATIVE = 2;

  private OtlpJsonEncoder() {}

  static byte[] encode(
      Map<String, String> resourceAttributes, HttpServerMetrics metrics, long timeUnixNanos) {
    ObjectNode root = MAPPER.createObjectNode();
    ObjectNode resourceMetric = root.putArray("resourceMetrics").addObject();
    ArrayNode resourceAttrs = resourceMetric.putObject("resource").putArray("attributes");
    resourceAttributes.forEach((key, value) -> addAttribute(resourceAttrs, key, value));

    ObjectNode scopeMetric = resourceMetric.putArray("scopeMetrics").addObject();
    scopeMetric.putObject("scope").put("name", "http_server");
    ArrayNode metricNodes = scopeMetric.putArray("metrics");

    List<HttpServerMetrics.MethodSnapshot> snapshots = metrics.snapshot();
    String serviceName = metrics.serviceName();
    long startNanos = metrics.startTimeUnixNanos();

    metricNodes.add(
        sum(
            "http_server_requests",
            "HTTP requests received",
            true,
            HttpServerMetrics.MethodSnapshot::requests,
            snapshots,
            serviceName,
            startNanos,
            timeUnixNanos));
    metricNodes.add(
        sum(
            "http_server_requests_success",
            "HTTP requests completed successfully (2xx-3xx)",
            true,
            HttpServerMetrics.MethodSnapshot::success,
            snapshots,
            serviceName,
            startNanos,
            timeUnixNanos));
    metricNodes.add(
        sum(
            "http_server_requests_failure",
            "HTTP requests that returned 4xx or 5xx",
            true,
            HttpServerMetrics.MethodSnapshot::failure,
            snapshots,
            serviceName,
            startNanos,
            timeUnixNanos));
    metricNodes.add(
        sum(
            "http_server_requests_active_gauge",
            "HTTP requests currently in flight",
            false,
            HttpServerMetrics.MethodSnapshot::active,
            snapshots,
            serviceName,
            startNanos,
            timeUnixNanos));
    metricNodes.add(histogram(snapshots, serviceName, startNanos, timeUnixNanos));

    try {
      return MAPPER.writeValueAsBytes(root);
    } catch (JsonProcessingException e) {
      // The tree is built from strings and numbers only; serialization cannot fail.
      throw new IllegalStateException("failed to serialize OTLP payload", e);
    }
  }

  private static ObjectNode sum(
      String name,
      String description,
      boolean monotonic,
      ToLongFunction<HttpServerMetrics.MethodSnapshot> value,
      List<HttpServerMetrics.MethodSnapshot> snapshots,
      String serviceName,
      long startNanos,
      long timeNanos) {
    ObjectNode metric = MAPPER.createObjectNode();
    metric.put("name", name);
    metric.put("description", description);
    ObjectNode sum = metric.putObject("sum");
    sum.put("aggregationTemporality", AGGREGATION_TEMPORALITY_CUMULATIVE);
    sum.put("isMonotonic", monotonic);
    ArrayNode points = sum.putArray("dataPoints");
    for (HttpServerMetrics.MethodSnapshot snapshot : snapshots) {
      ObjectNode point = points.addObject();
      point.put("startTimeUnixNano", Long.toString(startNanos));
      point.put("timeUnixNano", Long.toString(timeNanos));
      point.put("asInt", Long.toString(value.applyAsLong(snapshot)));
      addPointAttributes(point, serviceName, snapshot.httpMethod());
    }
    return metric;
  }

  private static ObjectNode histogram(
      List<HttpServerMetrics.MethodSnapshot> snapshots,
      String serviceName,
      long startNanos,
      long timeNanos) {
    ObjectNode metric = MAPPER.createObjectNode();
    metric.put("name", "http_server_request_duration_microseconds");
    metric.put("description", "HTTP request duration in microseconds");
    ObjectNode histogram = metric.putObject("histogram");
    histogram.put("aggregationTemporality", AGGREGATION_TEMPORALITY_CUMULATIVE);
    ArrayNode points = histogram.putArray("dataPoints");
    for (HttpServerMetrics.MethodSnapshot snapshot : snapshots) {
      ObjectNode point = points.addObject();
      point.put("startTimeUnixNano", Long.toString(startNanos));
      point.put("timeUnixNano", Long.toString(timeNanos));
      point.put("count", Long.toString(snapshot.durationCount()));
      point.put("sum", snapshot.durationSumMicros());
      ArrayNode bucketCounts = point.putArray("bucketCounts");
      for (long count : snapshot.bucketCounts()) {
        bucketCounts.add(Long.toString(count));
      }
      ArrayNode bounds = point.putArray("explicitBounds");
      for (double bound : HttpServerMetrics.BUCKET_BOUNDS) {
        bounds.add(bound);
      }
      addPointAttributes(point, serviceName, snapshot.httpMethod());
    }
    return metric;
  }

  private static void addPointAttributes(ObjectNode point, String serviceName, String httpMethod) {
    ArrayNode attributes = point.putArray("attributes");
    addAttribute(attributes, "http_method", httpMethod);
    addAttribute(attributes, "service_name", serviceName);
  }

  private static void addAttribute(ArrayNode attributes, String key, String value) {
    ObjectNode attribute = attributes.addObject();
    attribute.put("key", key);
    attribute.putObject("value").put("stringValue", value);
  }
}
