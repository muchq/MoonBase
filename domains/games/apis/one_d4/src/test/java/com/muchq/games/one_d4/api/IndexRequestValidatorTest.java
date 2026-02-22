package com.muchq.games.one_d4.api;

import static org.assertj.core.api.Assertions.assertThatCode;
import static org.assertj.core.api.Assertions.assertThatThrownBy;

import com.muchq.games.one_d4.api.dto.IndexRequest;
import org.junit.Test;

public class IndexRequestValidatorTest {

  private final IndexRequestValidator validator = new IndexRequestValidator();

  @Test
  public void validRequest() {
    var request = new IndexRequest("hikaru", "CHESS_COM", "2024-03", "2024-03");
    assertThatCode(() -> validator.validate(request)).doesNotThrowAnyException();
  }

  @Test
  public void validRequest_multipleMonths() {
    var request = new IndexRequest("hikaru", "CHESS_COM", "2024-01", "2024-12");
    assertThatCode(() -> validator.validate(request)).doesNotThrowAnyException();
  }

  @Test
  public void rejectsNullPlayer() {
    var request = new IndexRequest(null, "CHESS_COM", "2024-03", "2024-03");
    assertThatThrownBy(() -> validator.validate(request))
        .isInstanceOf(IllegalArgumentException.class)
        .hasMessage("player is required");
  }

  @Test
  public void rejectsBlankPlayer() {
    var request = new IndexRequest("  ", "CHESS_COM", "2024-03", "2024-03");
    assertThatThrownBy(() -> validator.validate(request))
        .isInstanceOf(IllegalArgumentException.class)
        .hasMessage("player is required");
  }

  @Test
  public void rejectsNullPlatform() {
    var request = new IndexRequest("hikaru", null, "2024-03", "2024-03");
    assertThatThrownBy(() -> validator.validate(request))
        .isInstanceOf(IllegalArgumentException.class)
        .hasMessage("platform is required");
  }

  @Test
  public void rejectsUnsupportedPlatform() {
    var request = new IndexRequest("hikaru", "LICHESS", "2024-03", "2024-03");
    assertThatThrownBy(() -> validator.validate(request))
        .isInstanceOf(IllegalArgumentException.class)
        .hasMessageContaining("Unsupported platform: LICHESS");
  }

  @Test
  public void rejectsNullStartMonth() {
    var request = new IndexRequest("hikaru", "CHESS_COM", null, "2024-03");
    assertThatThrownBy(() -> validator.validate(request))
        .isInstanceOf(IllegalArgumentException.class)
        .hasMessage("startMonth is required");
  }

  @Test
  public void rejectsNullEndMonth() {
    var request = new IndexRequest("hikaru", "CHESS_COM", "2024-03", null);
    assertThatThrownBy(() -> validator.validate(request))
        .isInstanceOf(IllegalArgumentException.class)
        .hasMessage("endMonth is required");
  }

  @Test
  public void rejectsMalformedStartMonth() {
    var request = new IndexRequest("hikaru", "CHESS_COM", "March", "2024-03");
    assertThatThrownBy(() -> validator.validate(request))
        .isInstanceOf(IllegalArgumentException.class)
        .hasMessageContaining("startMonth must be in YYYY-MM format");
  }

  @Test
  public void rejectsMalformedEndMonth() {
    var request = new IndexRequest("hikaru", "CHESS_COM", "2024-03", "2024-13");
    assertThatThrownBy(() -> validator.validate(request))
        .isInstanceOf(IllegalArgumentException.class)
        .hasMessageContaining("endMonth must be in YYYY-MM format");
  }

  @Test
  public void rejectsStartAfterEnd() {
    var request = new IndexRequest("hikaru", "CHESS_COM", "2024-06", "2024-03");
    assertThatThrownBy(() -> validator.validate(request))
        .isInstanceOf(IllegalArgumentException.class)
        .hasMessage("startMonth must not be after endMonth");
  }

  @Test
  public void rejectsRangeOver12Months() {
    var request = new IndexRequest("hikaru", "CHESS_COM", "2023-01", "2024-02");
    assertThatThrownBy(() -> validator.validate(request))
        .isInstanceOf(IllegalArgumentException.class)
        .hasMessageContaining("Maximum range is 12 months, got 14");
  }

  @Test
  public void accepts12MonthRange() {
    var request = new IndexRequest("hikaru", "CHESS_COM", "2024-01", "2024-12");
    assertThatCode(() -> validator.validate(request)).doesNotThrowAnyException();
  }
}
