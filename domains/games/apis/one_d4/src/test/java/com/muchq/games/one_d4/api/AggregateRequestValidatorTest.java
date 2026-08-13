package com.muchq.games.one_d4.api;

import static org.assertj.core.api.Assertions.assertThat;
import static org.assertj.core.api.Assertions.assertThatCode;
import static org.assertj.core.api.Assertions.assertThatThrownBy;

import com.muchq.games.chessql.compiler.AggregateSpec;
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

  /**
   * The validator hands the compiler a spec rather than a nod, so what it accepted and what gets
   * compiled are the same object. A validator that returned void could agree the request is fine
   * and still have the controller build something else from the raw fields.
   */
  @Test
  public void validateReturnsTheSpecTheCompilerWillUse() {
    AggregateSpec spec =
        validator.validate(
            new AggregateRequest(
                "outcome = \"win\"", List.of("opening_family"), "score", 20, "hikaru", 5));

    assertThat(spec.groupBy()).containsExactly("opening_family");
    assertThat(spec.player()).isEqualTo("hikaru");
    assertThat(spec.order()).isEqualTo(AggregateSpec.Order.SCORE);
    assertThat(spec.minGames()).isEqualTo(5);
    assertThat(spec.hasOutcomeMetrics()).isTrue();
  }

  @Test
  public void anAbsentOrderByIsCountAndNoPlayerMeansNoOutcomeMetrics() {
    AggregateSpec spec =
        validator.validate(new AggregateRequest("white.elo > 1", List.of("eco"), null, 20));

    assertThat(spec.order()).isEqualTo(AggregateSpec.Order.COUNT);
    assertThat(spec.hasOutcomeMetrics()).isFalse();
    assertThat(spec.minGames()).isZero();
  }

  /** Score is a player's score; ranking by it with nobody to score for is refused here. */
  @Test
  public void orderByScoreWithoutAPlayerRejected() {
    assertThatThrownBy(
            () ->
                validator.validate(
                    new AggregateRequest("white.elo > 1", List.of("eco"), "score", 20)))
        .isInstanceOf(IllegalArgumentException.class)
        .hasMessageContaining("requires a player");
  }

  @Test
  public void orderByScoreWithAPlayerPasses() {
    assertThatCode(
            () ->
                validator.validate(
                    new AggregateRequest(
                        "outcome = \"win\"", List.of("eco"), "score", 20, "hikaru")))
        .doesNotThrowAnyException();
  }

  @Test
  public void aNegativeMinGamesIsClampedRatherThanRejected() {
    assertThat(
            validator
                .validate(new AggregateRequest("white.elo > 1", List.of("eco"), null, 20, null, -3))
                .minGames())
        .isZero();
  }
}
