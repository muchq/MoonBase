package com.muchq.platform.yodel.micronaut;

import com.muchq.platform.yodel.HttpMetricsPipeline;
import com.muchq.platform.yodel.HttpServerMetrics;
import io.micronaut.http.HttpAttributes;
import io.micronaut.http.HttpRequest;
import io.micronaut.http.MutableHttpResponse;
import io.micronaut.http.annotation.Filter;
import io.micronaut.http.filter.HttpServerFilter;
import io.micronaut.http.filter.ServerFilterChain;
import io.micronaut.http.filter.ServerFilterPhase;
import jakarta.inject.Inject;
import java.util.concurrent.atomic.AtomicBoolean;
import org.reactivestreams.Publisher;
import reactor.core.publisher.Mono;

/**
 * Counts every request into the shared http_server_* family. Runs in the METRICS filter phase —
 * outside auth and routing — so rejected and unmatched requests are measured too. Adding this
 * library to a Micronaut service's runtime classpath is the whole integration; export turns on when
 * OTEL_EXPORTER_OTLP_ENDPOINT is set (https://github.com/muchq/MoonBase/issues/1212).
 */
@Filter(Filter.MATCH_ALL_PATTERN)
public class HttpServerMetricsFilter implements HttpServerFilter {
  /**
   * The route label for a request no route ever matched (404s, probes for paths that don't exist).
   * A fixed sentinel rather than the raw path, so scanners cannot mint unbounded series
   * (https://github.com/muchq/MoonBase/issues/1303).
   */
  public static final String UNMATCHED_ROUTE = "unmatched";

  private final HttpMetricsPipeline pipeline;

  /**
   * Takes the process-wide pipeline from {@link YodelMetricsFactory} rather than building its own,
   * so the service's custom instruments and this filter's HTTP counts leave on the same exporter.
   */
  @Inject
  public HttpServerMetricsFilter(HttpMetricsPipeline pipeline) {
    this.pipeline = pipeline;
  }

  /** For a filter constructed outside a bean context. */
  public HttpServerMetricsFilter() {
    this(HttpMetricsPipeline.fromEnv());
  }

  public HttpServerMetrics metrics() {
    return pipeline.metrics();
  }

  @Override
  public int getOrder() {
    return ServerFilterPhase.METRICS.order();
  }

  @Override
  public Publisher<MutableHttpResponse<?>> doFilter(
      HttpRequest<?> request, ServerFilterChain chain) {
    HttpServerMetrics metrics = pipeline.metrics();
    // getMethod(), not getMethodName(): the netty request's method name is
    // the raw wire token, so a scanner spraying invented verbs would mint a
    // label value (and a gauge entry) per token. The enum collapses them
    // all to CUSTOM, which keeps every key in this family bounded — the
    // same reason the route label is the matched template (#1303).
    String method = request.getMethod().name();
    long startNanos = System.nanoTime();
    metrics.recordRequestStart(method);
    AtomicBoolean recorded = new AtomicBoolean();
    return Mono.from(chain.proceed(request))
        .doOnNext(
            response -> {
              if (recorded.compareAndSet(false, true)) {
                metrics.recordRequestComplete(
                    method,
                    routeOf(request),
                    response.code(),
                    (System.nanoTime() - startNanos) / 1000.0);
              }
            })
        .doOnError(
            t -> {
              if (recorded.compareAndSet(false, true)) {
                metrics.recordRequestComplete(
                    method, routeOf(request), 500, (System.nanoTime() - startNanos) / 1000.0);
              }
            })
        .doFinally(
            signal -> {
              // Cancellation (client gone before a response) emits neither next nor
              // error; only the gauge and the request count move, like server_pal's
              // drop guard.
              if (recorded.compareAndSet(false, true)) {
                metrics.recordRequestAbandoned(method, routeOf(request));
              }
            });
  }

  /**
   * The matched route template ("/games/{id}"), never the raw path, so the label stays bounded
   * (#1303). This filter runs before routing — deliberately, so unmatched requests are measured too
   * — which is why the route is read here, on the far side of {@code chain.proceed}, where the
   * router has stamped its match onto the request. A request that matched nothing keeps the
   * sentinel.
   *
   * <p>{@code HttpAttributes.URI_TEMPLATE} is deprecated {@code forRemoval} since Micronaut 4.8 in
   * favor of {@code io.micronaut.web.router.RouteAttributes}; it is still what both the native
   * router and micronaut-jaxrs stamp today, and the filter tests break loudly if an upgrade stops
   * populating it — swap the accessor then, not speculatively.
   */
  private static String routeOf(HttpRequest<?> request) {
    return request.getAttribute(HttpAttributes.URI_TEMPLATE, String.class).orElse(UNMATCHED_ROUTE);
  }

  // No @PreDestroy closing the pipeline. It used to be right — the filter built the pipeline in
  // its own constructor and was the only thing holding it. Now YodelMetricsFactory owns it and
  // declares preDestroy = "close", so closing it here as well would shut the same exporter down
  // twice: Micronaut destroys dependents before their dependencies, so the filter's hook would
  // run first and stop the exporter while the service's own beans are still recording into the
  // CustomMetrics it drains.
  //
  // Pinned by theFilterDeclaresNoDestroyHookForAPipelineItDoesNotOwn, which also covers the
  // AutoCloseable route — Micronaut closes those with no annotation at all. The redundant flush
  // is separately defused in OtlpHttpMetricsExporter.close(); the ordering is not, which is why
  // the hook stays gone rather than being made safe.
}
