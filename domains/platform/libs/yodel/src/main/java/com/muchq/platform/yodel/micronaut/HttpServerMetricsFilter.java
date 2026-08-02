package com.muchq.platform.yodel.micronaut;

import com.muchq.platform.yodel.HttpMetricsPipeline;
import com.muchq.platform.yodel.HttpServerMetrics;
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
    String method = request.getMethodName();
    long startNanos = System.nanoTime();
    metrics.recordRequestStart(method);
    AtomicBoolean recorded = new AtomicBoolean();
    return Mono.from(chain.proceed(request))
        .doOnNext(
            response -> {
              if (recorded.compareAndSet(false, true)) {
                metrics.recordRequestComplete(
                    method, response.code(), (System.nanoTime() - startNanos) / 1000.0);
              }
            })
        .doOnError(
            t -> {
              if (recorded.compareAndSet(false, true)) {
                metrics.recordRequestComplete(
                    method, 500, (System.nanoTime() - startNanos) / 1000.0);
              }
            })
        .doFinally(
            signal -> {
              // Cancellation (client gone before a response) emits neither next nor
              // error; only the in-flight gauge moves, like server_pal's drop guard.
              if (recorded.compareAndSet(false, true)) {
                metrics.recordRequestAbandoned(method);
              }
            });
  }

  // No @PreDestroy closing the pipeline. It used to be right — the filter built the pipeline in
  // its own constructor and was the only thing holding it. Now YodelMetricsFactory owns it and
  // declares preDestroy = "close", so closing it here as well would shut the same exporter down
  // twice. OtlpHttpMetricsExporter.close() is not idempotent about its side effect: it runs a
  // final synchronous export, so a double close means two OTLP POSTs on the shutdown path, each
  // able to block for the request timeout. Worse, Micronaut destroys dependents before their
  // dependencies, so the filter would stop the exporter while the service's own beans are still
  // recording into the CustomMetrics it drains.
}
