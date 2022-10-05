package com.muchq.lunarcat.providers;

import com.fasterxml.jackson.annotation.JsonProperty;

public class ErrorResponse {

  private final String message;

  public ErrorResponse(String message) {
    this.message = message;
  }

  @JsonProperty("message")
  public String getMessage() {
    return message;
  }
}
