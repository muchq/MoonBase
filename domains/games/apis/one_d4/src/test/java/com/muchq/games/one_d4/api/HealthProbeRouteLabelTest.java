package com.muchq.games.one_d4.api;

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
 * The #1303 contract has three parties: yodel emits the route label, prom_proxy subtracts {@code
 * route="/health"}, and one_d4's healthcheck traffic must actually arrive under that exact string —
 * across the micronaut-jaxrs bridge, whose route templates nothing else pins. If the bridge stamped
 * anything else ("/health/", the unmatched sentinel), the Serving numbers would keep the probe
 * floor and the Probes tile would read zero, each looking exactly like health.
 */
public class HealthProbeRouteLabelTest {

  private EmbeddedServer server;
  private HttpClient client;
  private String baseUrl;

  @BeforeEach
  public void setUp() {
    server =
        ApplicationContext.run(
            EmbeddedServer.class,
            Map.of(
                "indexer.db.url",
                "jdbc:h2:mem:health_probe_route_test_" + System.nanoTime() + ";DB_CLOSE_DELAY=-1",
                "micronaut.server.port",
                "-1"));
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
    // Named the sentinel explicitly: a jaxrs template the router failed to
    // stamp would land there, and "no /health series" alone would not say
    // which of the two failure shapes happened.
    assertThat(filter.metrics().snapshot())
        .filteredOn(s -> s.route().equals(HttpServerMetricsFilter.UNMATCHED_ROUTE))
        .isEmpty();
  }
}
