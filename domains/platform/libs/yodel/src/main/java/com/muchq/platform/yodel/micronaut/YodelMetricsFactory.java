package com.muchq.platform.yodel.micronaut;

import com.muchq.platform.yodel.CustomMetrics;
import com.muchq.platform.yodel.HttpMetricsPipeline;
import io.micronaut.context.annotation.Bean;
import io.micronaut.context.annotation.Factory;
import jakarta.inject.Singleton;

/**
 * Makes the process's metrics pipeline injectable, so a service can record its own work into the
 * same exporter that already carries the {@code http_server_*} family.
 *
 * <p>Before this the filter built a pipeline privately in its own constructor and nothing else
 * could reach it. That was fine while HTTP counts were the only instruments — and it is exactly why
 * every Java entry in prom_proxy's registry was empty: there was somewhere to record custom metrics
 * to, but no way for application code to get hold of it
 * (https://github.com/muchq/MoonBase/issues/1212).
 *
 * <p>One pipeline per process, not one per injection point. Two would mean two exporters posting
 * two partial views of the same service on the same interval, and the dashboard summing across them
 * would double-count every request.
 */
@Factory
public class YodelMetricsFactory {

  @Singleton
  @Bean(preDestroy = "close")
  HttpMetricsPipeline httpMetricsPipeline() {
    return HttpMetricsPipeline.fromEnv();
  }

  /**
   * The service's own instruments. Injectable on its own so application code never has to know the
   * pipeline exists, and records harmlessly into memory when no OTEL endpoint is configured.
   */
  @Singleton
  CustomMetrics customMetrics(HttpMetricsPipeline pipeline) {
    return pipeline.custom();
  }
}
