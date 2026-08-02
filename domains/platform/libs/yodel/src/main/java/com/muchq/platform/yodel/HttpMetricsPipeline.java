package com.muchq.platform.yodel;

import java.time.Duration;
import java.util.LinkedHashMap;
import java.util.Map;
import org.jspecify.annotations.Nullable;

/**
 * How a Java service joins the shared observability rails: reads the standard OTEL_* env vars
 * (OTEL_EXPORTER_OTLP_ENDPOINT, OTEL_SERVICE_NAME, OTEL_RESOURCE_ATTRIBUTES) and assembles the
 * http_server_* instruments plus a periodic OTLP exporter. Without the endpoint the instruments
 * still record in memory and nothing exports — the same no-op contract as server_pal's init_otel.
 */
public final class HttpMetricsPipeline implements AutoCloseable {
  private static final Duration EXPORT_INTERVAL = Duration.ofSeconds(15);

  private final HttpServerMetrics metrics;
  private final CustomMetrics custom;
  private final @Nullable OtlpHttpMetricsExporter exporter;

  private HttpMetricsPipeline(
      HttpServerMetrics metrics, CustomMetrics custom, @Nullable OtlpHttpMetricsExporter exporter) {
    this.metrics = metrics;
    this.custom = custom;
    this.exporter = exporter;
  }

  public static HttpMetricsPipeline fromEnv() {
    return fromEnv(System.getenv());
  }

  static HttpMetricsPipeline fromEnv(Map<String, String> env) {
    String serviceName = env.getOrDefault("OTEL_SERVICE_NAME", "");
    HttpServerMetrics metrics =
        new HttpServerMetrics(serviceName, System.currentTimeMillis() * 1_000_000L);
    CustomMetrics custom = new CustomMetrics();
    String endpoint = env.get("OTEL_EXPORTER_OTLP_ENDPOINT");
    if (endpoint == null || endpoint.isBlank()) {
      return new HttpMetricsPipeline(metrics, custom, null);
    }
    OtlpHttpMetricsExporter exporter =
        new OtlpHttpMetricsExporter(
            endpoint, resourceAttributes(env, serviceName), metrics, custom, EXPORT_INTERVAL);
    exporter.start();
    return new HttpMetricsPipeline(metrics, custom, exporter);
  }

  /**
   * OTEL_RESOURCE_ATTRIBUTES ("k=v,k=v") with OTEL_SERVICE_NAME winning for service.name — the same
   * precedence the SDK resource detectors implement.
   */
  static Map<String, String> resourceAttributes(Map<String, String> env, String serviceName) {
    Map<String, String> attributes = new LinkedHashMap<>();
    for (String pair : env.getOrDefault("OTEL_RESOURCE_ATTRIBUTES", "").split(",")) {
      int eq = pair.indexOf('=');
      if (eq > 0 && eq < pair.length() - 1) {
        attributes.put(pair.substring(0, eq).trim(), pair.substring(eq + 1).trim());
      }
    }
    if (!serviceName.isEmpty()) {
      attributes.put("service.name", serviceName);
    }
    return attributes;
  }

  public HttpServerMetrics metrics() {
    return metrics;
  }

  /**
   * The service's own instruments. Always present, exported on the same interval as the HTTP family
   * when an endpoint is configured and recording harmlessly into memory when it is not — so a
   * caller never has to branch on whether observability is switched on.
   */
  public CustomMetrics custom() {
    return custom;
  }

  @Override
  public void close() {
    if (exporter != null) {
      exporter.close();
    }
  }
}
