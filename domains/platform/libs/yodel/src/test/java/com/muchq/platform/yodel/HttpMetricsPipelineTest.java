package com.muchq.platform.yodel;

import static org.assertj.core.api.Assertions.assertThat;

import java.util.Map;
import org.junit.jupiter.api.Test;

public class HttpMetricsPipelineTest {

  @Test
  public void withoutEndpointRecordsInMemoryAndNeverExports() {
    try (HttpMetricsPipeline pipeline = HttpMetricsPipeline.fromEnv(Map.of())) {
      pipeline.metrics().recordRequestStart("GET");
      assertThat(pipeline.metrics().snapshot()).hasSize(1);
      assertThat(pipeline.metrics().serviceName()).isEmpty();
    }
  }

  @Test
  public void serviceNameComesFromTheStandardEnvVar() {
    try (HttpMetricsPipeline pipeline =
        HttpMetricsPipeline.fromEnv(Map.of("OTEL_SERVICE_NAME", "one_d4"))) {
      assertThat(pipeline.metrics().serviceName()).isEqualTo("one_d4");
    }
  }

  @Test
  public void resourceAttributesParseWithServiceNamePrecedence() {
    Map<String, String> attributes =
        HttpMetricsPipeline.resourceAttributes(
            Map.of(
                "OTEL_RESOURCE_ATTRIBUTES",
                "service.name=wrong, service.version=abc123 ,malformed,=alsobad",
                "OTEL_SERVICE_NAME",
                "one_d4"),
            "one_d4");

    assertThat(attributes)
        .containsEntry("service.name", "one_d4")
        .containsEntry("service.version", "abc123")
        .hasSize(2);
  }

  @Test
  public void emptyEnvironmentYieldsNoResourceAttributes() {
    assertThat(HttpMetricsPipeline.resourceAttributes(Map.of(), "")).isEmpty();
  }
}
