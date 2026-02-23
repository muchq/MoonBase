package com.muchq.games.one_d4.api;

import static org.assertj.core.api.Assertions.assertThatCode;
import static org.assertj.core.api.Assertions.assertThatThrownBy;

import com.muchq.games.one_d4.api.dto.QueryRequest;
import org.junit.Test;

public class QueryRequestValidatorTest {

  private final QueryRequestValidator validator = new QueryRequestValidator();

  @Test
  public void validQuery() {
    var request = new QueryRequest("motif(fork)", 10, 0);
    assertThatCode(() -> validator.validate(request)).doesNotThrowAnyException();
  }

  @Test
  public void rejectsNullQuery() {
    var request = new QueryRequest(null, 10, 0);
    assertThatThrownBy(() -> validator.validate(request))
        .isInstanceOf(IllegalArgumentException.class)
        .hasMessage("query is required");
  }

  @Test
  public void rejectsBlankQuery() {
    var request = new QueryRequest("   ", 10, 0);
    assertThatThrownBy(() -> validator.validate(request))
        .isInstanceOf(IllegalArgumentException.class)
        .hasMessage("query is required");
  }

  @Test
  public void rejectsQueryOverMaxLength() {
    var longQuery = "a".repeat(4097);
    var request = new QueryRequest(longQuery, 10, 0);
    assertThatThrownBy(() -> validator.validate(request))
        .isInstanceOf(IllegalArgumentException.class)
        .hasMessageContaining("query exceeds maximum length");
  }

  @Test
  public void acceptsQueryAtMaxLength() {
    var query = "a".repeat(4096);
    var request = new QueryRequest(query, 10, 0);
    assertThatCode(() -> validator.validate(request)).doesNotThrowAnyException();
  }
}
