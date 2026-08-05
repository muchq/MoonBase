package com.muchq.games.mcpserver.api;

import static org.assertj.core.api.Assertions.assertThat;

import com.muchq.platform.yodel.micronaut.HttpServerMetricsFilter;
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

/**
 * The #1307 contract has three parties on this service: the compose healthcheck probes GET
 * /health, HealthController answers it, and the probe traffic must land under exactly {@code
 * route="/health"} so prom_proxy's probeFilter subtracts it from the Serving numbers and the
 * Probes tile can chart it. This boots the real server and reads the filter's snapshot, the same
 * shape as one_d4's HealthProbeRouteLabelTest.
 */
public class HealthProbeRouteLabelTest {

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

  @Test
  public void theProbesRequestsLandUnderTheExactRouteProbeFilterSubtracts() throws Exception {
    HttpRequest request =
        HttpRequest.newBuilder().uri(URI.create(baseUrl + "/health")).GET().build();
    HttpResponse<String> response = client.send(request, HttpResponse.BodyHandlers.ofString());
    assertThat(response.statusCode()).isEqualTo(200);
    assertThat(response.body()).contains("\"status\":\"UP\"");

    HttpServerMetricsFilter filter =
        server.getApplicationContext().getBean(HttpServerMetricsFilter.class);
    assertThat(filter.metrics().snapshot())
        .filteredOn(s -> s.route().equals("/health"))
        .singleElement()
        .satisfies(
            s -> {
              assertThat(s.httpMethod()).isEqualTo("GET");
              assertThat(s.requests()).isEqualTo(1);
            });
    // Named the sentinel explicitly: a route the router failed to stamp would
    // land there, and "no /health series" alone would not say which of the
    // two failure shapes happened.
    assertThat(filter.metrics().snapshot())
        .filteredOn(s -> s.route().equals(HttpServerMetricsFilter.UNMATCHED_ROUTE))
        .isEmpty();
  }
}
