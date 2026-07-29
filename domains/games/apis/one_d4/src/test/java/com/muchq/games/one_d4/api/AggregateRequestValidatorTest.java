package com.muchq.games.one_d4.api;

import static org.assertj.core.api.Assertions.assertThatCode;
import static org.assertj.core.api.Assertions.assertThatThrownBy;

import com.muchq.games.one_d4.api.dto.AggregateRequest;
import java.util.List;
import org.junit.jupiter.api.Test;

public class AggregateRequestValidatorTest {

  private final AggregateRequestValidator validator = new AggregateRequestValidator();

  @Test
  public void validRequestPasses() {
    assertThatCode(
            () ->
                validator.validate(
                    new AggregateRequest(
                        "white.username = \"hikaru\"", List.of("opening_family"), "count", 20)))
        .doesNotThrowAnyException();
  }

  @Test
  public void nullOrBlankQueryRejected() {
    assertThatThrownBy(
            () -> validator.validate(new AggregateRequest(null, List.of("eco"), null, 20)))
        .isInstanceOf(IllegalArgumentException.class)
        .hasMessageContaining("query is required");
    assertThatThrownBy(
            () -> validator.validate(new AggregateRequest("  ", List.of("eco"), null, 20)))
        .isInstanceOf(IllegalArgumentException.class)
        .hasMessageContaining("query is required");
  }

  @Test
  public void oversizedQueryRejected() {
    String longQuery = "white.elo > 1".repeat(400);
    assertThatThrownBy(
            () -> validator.validate(new AggregateRequest(longQuery, List.of("eco"), null, 20)))
        .isInstanceOf(IllegalArgumentException.class)
        .hasMessageContaining("maximum length");
  }

  @Test
  public void missingOrEmptyGroupByRejected() {
    assertThatThrownBy(
            () -> validator.validate(new AggregateRequest("white.elo > 1", null, null, 20)))
        .isInstanceOf(IllegalArgumentException.class)
        .hasMessageContaining("groupBy");
    assertThatThrownBy(
            () -> validator.validate(new AggregateRequest("white.elo > 1", List.of(), null, 20)))
        .isInstanceOf(IllegalArgumentException.class)
        .hasMessageContaining("groupBy");
  }

  @Test
  public void tooManyGroupByFieldsRejected() {
    assertThatThrownBy(
            () ->
                validator.validate(
                    new AggregateRequest(
                        "white.elo > 1",
                        List.of(
                            "eco",
                            "result",
                            "time_class",
                            "platform",
                            "white_title",
                            "black_title"),
                        null,
                        20)))
        .isInstanceOf(IllegalArgumentException.class)
        .hasMessageContaining("at most");
  }

  @Test
  public void unsupportedOrderByRejected() {
    assertThatThrownBy(
            () ->
                validator.validate(
                    new AggregateRequest("white.elo > 1", List.of("eco"), "elo", 20)))
        .isInstanceOf(IllegalArgumentException.class)
        .hasMessageContaining("orderBy");
  }
}
