package com.muchq.platform.yodel;

import static org.assertj.core.api.Assertions.assertThat;

import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.ObjectMapper;
import com.sun.net.httpserver.HttpServer;
import java.net.InetSocketAddress;
import java.time.Duration;
import java.util.Map;
import java.util.concurrent.ArrayBlockingQueue;
import java.util.concurrent.BlockingQueue;
import java.util.concurrent.TimeUnit;
import org.junit.jupiter.api.AfterEach;
import org.junit.jupiter.api.BeforeEach;
import org.junit.jupiter.api.Test;

public class OtlpHttpMetricsExporterTest {
  private record Received(String path, String contentType, byte[] body) {}

  private HttpServer server;
  private final BlockingQueue<Received> received = new ArrayBlockingQueue<>(4);

  @BeforeEach
  public void setUp() throws Exception {
    server = HttpServer.create(new InetSocketAddress("127.0.0.1", 0), 0);
    server.createContext(
        "/",
        exchange -> {
          received.add(
              new Received(
                  exchange.getRequestURI().getPath(),
                  exchange.getRequestHeaders().getFirst("Content-Type"),
                  exchange.getRequestBody().readAllBytes()));
          exchange.sendResponseHeaders(200, -1);
          exchange.close();
        });
    server.start();
  }

  @AfterEach
  public void tearDown() {
    server.stop(0);
  }

  @Test
  public void postsOtlpJsonToTheMetricsPath() throws Exception {
    HttpServerMetrics metrics = new HttpServerMetrics("svc", 1_000_000_000L);
    metrics.recordRequestStart("GET");
    metrics.recordRequestComplete("GET", 200, 42.0);

    String endpoint = "http://127.0.0.1:" + server.getAddress().getPort();
    OtlpHttpMetricsExporter exporter =
        new OtlpHttpMetricsExporter(
            endpoint, Map.of("service.name", "svc"), metrics, Duration.ofHours(1));
    exporter.exportOnce();

    Received request = received.poll(5, TimeUnit.SECONDS);
    assertThat(request).isNotNull();
    assertThat(request.path()).isEqualTo("/v1/metrics");
    assertThat(request.contentType()).isEqualTo("application/json");

    JsonNode payload = new ObjectMapper().readTree(request.body());
    assertThat(payload.at("/resourceMetrics/0/scopeMetrics/0/metrics/0/name").asText())
        .isEqualTo("http_server_requests");
  }
}
