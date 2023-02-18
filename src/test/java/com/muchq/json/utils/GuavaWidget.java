package com.muchq.json.utils;

import com.fasterxml.jackson.annotation.JsonCreator;
import com.fasterxml.jackson.annotation.JsonProperty;
import com.google.common.base.Optional;

public class GuavaWidget {

  private final Optional<Integer> foo;

  @JsonCreator
  public GuavaWidget(@JsonProperty("foo") Integer foo) {
    this.foo = Optional.fromNullable(foo);
  }

  public Optional<Integer> getFoo() {
    return foo;
  }

  @Override
  public int hashCode() {
    return foo.hashCode();
  }

  @Override
  public boolean equals(Object other) {
    return other instanceof GuavaWidget && ((GuavaWidget) other).getFoo().equals(foo);
  }
}
