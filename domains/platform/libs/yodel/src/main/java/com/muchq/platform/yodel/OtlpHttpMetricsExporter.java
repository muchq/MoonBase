package com.muchq.platform.yodel;

import java.io.IOException;
import java.net.URI;
import java.net.http.HttpClient;
import java.net.http.HttpRequest;
import java.net.http.HttpResponse;
import java.time.Duration;
import java.util.Map;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

/**
 * Periodically POSTs the http_server_* snapshot as OTLP JSON to {@code <endpoint>/v1/metrics} from
 * a daemon thread. Export failures are logged and retried on the next tick; they never affect
 * serving.
 */
public final class OtlpHttpMetricsExporter implements AutoCloseable {
  private static final Logger LOG = LoggerFactory.getLogger(OtlpHttpMetricsExporter.class);
  private static final Duration REQUEST_TIMEOUT = Duration.ofSeconds(5);

  private final URI metricsUri;
  private final Map<String, String> resourceAttributes;
  private final HttpServerMetrics metrics;
  private final CustomMetrics custom;
  private final Duration interval;
  private final HttpClient client;
  private final Thread thread;
  private volatile boolean started;
  private volatile boolean closed;

  public OtlpHttpMetricsExporter(
      String endpoint,
      Map<String, String> resourceAttributes,
      HttpServerMetrics metrics,
      Duration interval) {
    this(endpoint, resourceAttributes, metrics, new CustomMetrics(), interval);
  }

  public OtlpHttpMetricsExporter(
      String endpoint,
      Map<String, String> resourceAttributes,
      HttpServerMetrics metrics,
      CustomMetrics custom,
      Duration interval) {
    String base = endpoint.endsWith("/") ? endpoint.substring(0, endpoint.length() - 1) : endpoint;
    this.metricsUri = URI.create(base + "/v1/metrics");
    this.resourceAttributes = Map.copyOf(resourceAttributes);
    this.metrics = metrics;
    this.custom = custom;
    this.interval = interval;
    this.client = HttpClient.newBuilder().connectTimeout(REQUEST_TIMEOUT).build();
    this.thread = new Thread(this::runLoop, "yodel-otlp-exporter");
    this.thread.setDaemon(true);
  }

  public void start() {
    started = true;
    thread.start();
    LOG.info("OTel metrics initialised (endpoint: {})", metricsUri);
  }

  private void runLoop() {
    while (!closed) {
      try {
        Thread.sleep(interval.toMillis());
      } catch (InterruptedException e) {
        return;
      }
      if (!closed) {
        exportOnce();
      }
    }
  }

  // Package-private so tests can drive an export without waiting out the interval.
  void exportOnce() {
    byte[] body =
        OtlpJsonEncoder.encode(
            resourceAttributes, metrics, custom, System.currentTimeMillis() * 1_000_000L);
    HttpRequest request =
        HttpRequest.newBuilder(metricsUri)
            .timeout(REQUEST_TIMEOUT)
            .header("Content-Type", "application/json")
            .POST(HttpRequest.BodyPublishers.ofByteArray(body))
            .build();
    try {
      HttpResponse<Void> response = client.send(request, HttpResponse.BodyHandlers.discarding());
      if (response.statusCode() / 100 != 2) {
        LOG.warn("OTLP export to {} returned {}", metricsUri, response.statusCode());
      }
    } catch (IOException e) {
      LOG.warn("OTLP export to {} failed: {}", metricsUri, e.toString());
    } catch (InterruptedException e) {
      Thread.currentThread().interrupt();
    }
  }

  @Override
  public void close() {
    closed = true;
    thread.interrupt();
    if (started) {
      // Final flush so the last interval's counts aren't lost on clean shutdown.
      exportOnce();
    }
  }
}
