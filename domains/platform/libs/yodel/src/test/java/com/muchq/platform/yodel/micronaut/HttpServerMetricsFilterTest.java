package com.muchq.platform.yodel.micronaut;

import static org.assertj.core.api.Assertions.assertThat;

import com.muchq.platform.yodel.HttpServerMetrics;
import io.micronaut.context.ApplicationContext;
import io.micronaut.runtime.server.EmbeddedServer;
import java.net.URI;
import java.net.http.HttpClient;
import java.net.http.HttpRequest;
import java.net.http.HttpResponse;
import java.util.Map;
import org.junit.jupiter.api.AfterEach;
import org.junit.jupiter.api.BeforeEach;
import org.junit.jupiter.api.Test;

public class HttpServerMetricsFilterTest {

  private EmbeddedServer server;
  private HttpClient client;
  private String baseUrl;

  @BeforeEach
  public void setUp() {
    server = ApplicationContext.run(EmbeddedServer.class, Map.of("micronaut.server.port", "-1"));
    client = HttpClient.newHttpClient();
    baseUrl = "http://localhost:" + server.getPort();
  }

  @AfterEach
  public void tearDown() {
    server.stop();
  }

  private int get(String path) throws Exception {
    HttpRequest request = HttpRequest.newBuilder().uri(URI.create(baseUrl + path)).GET().build();
    return client.send(request, HttpResponse.BodyHandlers.discarding()).statusCode();
  }

  @Test
  public void countsMatchedFailingAndUnmatchedRequests() throws Exception {
    assertThat(get("/yodel-test/ok")).isEqualTo(200);
    assertThat(get("/yodel-test/boom")).isEqualTo(500);
    assertThat(get("/no-such-route")).isEqualTo(404);

    HttpServerMetricsFilter filter =
        server.getApplicationContext().getBean(HttpServerMetricsFilter.class);
    HttpServerMetrics.MethodSnapshot get =
        filter.metrics().snapshot().stream()
            .filter(s -> s.httpMethod().equals("GET"))
            .findFirst()
            .orElseThrow();

    assertThat(get.requests()).isEqualTo(3);
    assertThat(get.success()).isEqualTo(1);
    assertThat(get.failure()).isEqualTo(2);
    assertThat(get.active()).isZero();
    assertThat(get.durationCount()).isEqualTo(3);
    assertThat(get.durationSumMicros()).isGreaterThan(0.0);
  }
}
