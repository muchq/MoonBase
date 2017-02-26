package com.muchq.lunarcat;

import com.fasterxml.jackson.annotation.JsonCreator;
import com.fasterxml.jackson.annotation.JsonProperty;

public class Widget {
  private final String message;

  @JsonCreator
  public Widget(@JsonProperty("message") String message) {
    this.message = message;
  }

  @JsonProperty("message")
  public String getMessage() {
    return message;
  }
}
