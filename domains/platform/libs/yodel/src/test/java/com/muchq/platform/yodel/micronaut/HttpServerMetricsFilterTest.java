package com.muchq.platform.yodel.micronaut;

import static org.assertj.core.api.Assertions.assertThat;

import com.muchq.platform.yodel.CustomMetrics;
import com.muchq.platform.yodel.HttpMetricsPipeline;
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
    java.util.List<HttpServerMetrics.RouteSnapshot> gets =
        filter.metrics().snapshot().stream().filter(s -> s.httpMethod().equals("GET")).toList();

    long requests = gets.stream().mapToLong(HttpServerMetrics.RouteSnapshot::requests).sum();
    long success = gets.stream().mapToLong(HttpServerMetrics.RouteSnapshot::success).sum();
    long failure = gets.stream().mapToLong(HttpServerMetrics.RouteSnapshot::failure).sum();
    long durations = gets.stream().mapToLong(HttpServerMetrics.RouteSnapshot::durationCount).sum();
    assertThat(requests).isEqualTo(3);
    assertThat(success).isEqualTo(1);
    assertThat(failure).isEqualTo(2);
    assertThat(durations).isEqualTo(3);
    assertThat(filter.metrics().activeSnapshot().get(0).active()).isZero();
  }

  /**
   * The #1303 point: probe traffic must be separable from serving traffic by label, which takes the
   * matched route template — read after routing, since this filter deliberately runs before it —
   * and a fixed sentinel for requests no route matched, so scanners cannot mint unbounded series.
   */
  @Test
  public void routesAreTheMatchedTemplatesAndUnmatchedIsASentinel() throws Exception {
    assertThat(get("/yodel-test/ok")).isEqualTo(200);
    assertThat(get("/yodel-test/boom")).isEqualTo(500);
    // The parameterized route is the load-bearing case: two raw paths,
    // one template — the only shape where "matched template, never the
    // raw path" is observable rather than vacuously true.
    assertThat(get("/yodel-test/widgets/1")).isEqualTo(200);
    assertThat(get("/yodel-test/widgets/2")).isEqualTo(200);
    assertThat(get("/no-such-route")).isEqualTo(404);
    assertThat(get("/another-miss")).isEqualTo(404);

    HttpServerMetricsFilter filter =
        server.getApplicationContext().getBean(HttpServerMetricsFilter.class);
    // Keyed by method + route, the snapshot's own key: a merge collision
    // here should fail an assertion below, not throw out of toMap.
    java.util.Map<String, Long> requestsByRoute =
        filter.metrics().snapshot().stream()
            .collect(
                java.util.stream.Collectors.toMap(
                    s -> s.httpMethod() + " " + s.route(),
                    HttpServerMetrics.RouteSnapshot::requests,
                    Long::sum));

    assertThat(requestsByRoute)
        .containsEntry("GET /yodel-test/ok", 1L)
        .containsEntry("GET /yodel-test/boom", 1L)
        .containsEntry("GET /yodel-test/widgets/{id}", 2L)
        .containsEntry("GET " + HttpServerMetricsFilter.UNMATCHED_ROUTE, 2L);
    // Four distinct raw paths beyond the fixed ones, two bounded series
    // (one template, one sentinel): the label cannot explode.
    assertThat(requestsByRoute).hasSize(4);
  }

  /**
   * A wrong-method request to an existing path keeps the sentinel on this rail: the router stamps
   * no template on a 405, so {@code routeOf} falls through to {@code UNMATCHED_ROUTE}, and the
   * request stays in the Serving numbers. This is a deliberate, pinned divergence from the C++ and
   * Rust rails, where any method on the health *path* keeps the {@code /health} literal (aura
   * compares the path; axum stamps MatchedPath on 405s). It only matters for scanner traffic on a
   * Java service's public /health — the compose probes are GET — and the safe direction: a scanner
   * POST cannot hide inside the probe subtraction here.
   */
  @Test
  public void wrongMethodOnAnExistingPathKeepsTheSentinel() throws Exception {
    HttpRequest request =
        HttpRequest.newBuilder()
            .uri(URI.create(baseUrl + "/yodel-test/ok"))
            .POST(HttpRequest.BodyPublishers.noBody())
            .build();
    assertThat(client.send(request, HttpResponse.BodyHandlers.discarding()).statusCode())
        .isEqualTo(405);

    HttpServerMetricsFilter filter =
        server.getApplicationContext().getBean(HttpServerMetricsFilter.class);
    assertThat(filter.metrics().snapshot())
        .filteredOn(s -> s.httpMethod().equals("POST"))
        .singleElement()
        .satisfies(s -> assertThat(s.route()).isEqualTo(HttpServerMetricsFilter.UNMATCHED_ROUTE));
  }

  /**
   * The method label has the same bounded-cardinality contract as the route label (#1303), on a
   * different mechanism: the filter reads {@code getMethod().name()}, the enum, which collapses
   * every verb it doesn't know to {@code CUSTOM}. Reverting to {@code getMethodName()} — the raw
   * wire token — hands a scanner spraying invented verbs a label value and a gauge entry per token.
   * Driven over a real socket so the netty parsing leg is the one measured.
   */
  @Test
  public void aCustomVerbCollapsesToTheBoundedMethodEnum() throws Exception {
    HttpRequest request =
        HttpRequest.newBuilder()
            .uri(URI.create(baseUrl + "/yodel-test/ok"))
            .method("BREW", HttpRequest.BodyPublishers.noBody())
            .build();
    client.send(request, HttpResponse.BodyHandlers.discarding());

    HttpServerMetricsFilter filter =
        server.getApplicationContext().getBean(HttpServerMetricsFilter.class);
    assertThat(filter.metrics().snapshot())
        .isNotEmpty()
        .extracting(HttpServerMetrics.RouteSnapshot::httpMethod)
        .containsOnly("CUSTOM");
    assertThat(filter.metrics().activeSnapshot())
        .extracting(HttpServerMetrics.ActiveSnapshot::httpMethod)
        .containsOnly("CUSTOM");
  }

  /**
   * A chain that throws synchronously — a filter further down blowing up during setup, before any
   * publisher exists — must still count the request. Without the defer around {@code
   * chain.proceed}, no Mono is ever constructed, none of the completion hooks fire, and the request
   * lands in the gauge but in no counter at all.
   */
  @Test
  public void aSynchronouslyThrowingChainStillCountsTheRequest() {
    // The server's own filter bean, driven directly: no request has gone
    // over the wire yet, so its snapshot holds only what this call records.
    HttpServerMetricsFilter filter =
        server.getApplicationContext().getBean(HttpServerMetricsFilter.class);
    io.micronaut.http.HttpRequest<?> request = io.micronaut.http.HttpRequest.GET("/kaboom");

    reactor.core.publisher.Mono.from(
            filter.doFilter(
                request,
                r -> {
                  throw new RuntimeException("filter chain exploded before producing a publisher");
                }))
        .onErrorComplete()
        .block();

    HttpServerMetrics.RouteSnapshot snapshot = filter.metrics().snapshot().get(0);
    assertThat(snapshot.route()).isEqualTo(HttpServerMetricsFilter.UNMATCHED_ROUTE);
    assertThat(snapshot.requests()).isEqualTo(1);
    assertThat(snapshot.failure()).isEqualTo(1);
    assertThat(filter.metrics().activeSnapshot().get(0).active()).isZero();
  }

  /**
   * One pipeline per process, which the factory's javadoc asserts and nothing was checking.
   *
   * <p>Micronaut factory methods are prototype-scoped unless annotated, so dropping
   * {@code @Singleton} gives the filter and the injected {@code CustomMetrics} different pipelines
   * — two exporter threads posting two partial views of the same service on the same interval, and
   * a dashboard summing across them double-counts every request. Nothing fails at boot; the numbers
   * are just quietly wrong.
   */
  @Test
  public void thePipelineIsOneInstanceSharedByTheFilterAndTheService() {
    HttpMetricsPipeline first = server.getApplicationContext().getBean(HttpMetricsPipeline.class);
    HttpMetricsPipeline second = server.getApplicationContext().getBean(HttpMetricsPipeline.class);
    assertThat(first).isSameAs(second);

    // And the injectable registry is that pipeline's own, not a detached one — otherwise a
    // service's recordings never reach the exporter.
    CustomMetrics custom = server.getApplicationContext().getBean(CustomMetrics.class);
    assertThat(custom).isSameAs(first.custom());

    HttpServerMetricsFilter filter =
        server.getApplicationContext().getBean(HttpServerMetricsFilter.class);
    assertThat(filter.metrics()).isSameAs(first.metrics());
  }

  /**
   * Sharing the pipeline means the filter no longer owns it, and the destroy hook has to move with
   * the ownership. {@link YodelMetricsFactory} declares {@code preDestroy = "close"}; a hook here
   * as well would close the same exporter twice.
   *
   * <p>Micronaut destroys dependents before their dependencies, so the filter's hook runs first —
   * stopping the exporter while the service's own beans are still recording into the {@code
   * CustomMetrics} it drains. That the second close is now a no-op ({@code
   * OtlpHttpMetricsExporterTest.aSecondCloseSendsNothingFurther}) limits the damage to a lost
   * shutdown flush rather than a duplicate one; it does not make the hook correct.
   *
   * <p>Checked reflectively because the failure is an annotation that is absent — there is no call
   * to assert on, and a behavioural test would need a stub OTLP endpoint injected through {@code
   * fromEnv}, which the factory does not offer. Both {@code Closeable} routes are covered too:
   * Micronaut gives any {@code AutoCloseable} bean an implicit destroy hook, so implementing the
   * interface reintroduces exactly what removing the annotation fixed.
   */
  @Test
  public void theFilterDeclaresNoDestroyHookForAPipelineItDoesNotOwn() {
    assertThat(AutoCloseable.class.isAssignableFrom(HttpServerMetricsFilter.class))
        .as("an AutoCloseable bean is closed by Micronaut without any annotation")
        .isFalse();

    assertThat(HttpServerMetricsFilter.class.getDeclaredMethods())
        .filteredOn(m -> m.isAnnotationPresent(jakarta.annotation.PreDestroy.class))
        .as("the factory owns the pipeline's lifecycle; the filter is a consumer")
        .isEmpty();
  }
}
