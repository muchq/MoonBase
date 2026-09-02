package com.muchq.games.one_d4.api;

import com.muchq.platform.yodel.CustomMetrics;
import com.muchq.platform.yodel.HttpMetricsPipeline;

/** A QueryEvents over an in-memory pipeline, for tests that build controllers by hand. */
final class TestQueryEvents {
  private TestQueryEvents() {}

  static CustomMetrics metrics() {
    return HttpMetricsPipeline.fromEnv().custom();
  }

  static QueryEvents create() {
    return new QueryEvents(metrics());
  }
}
