package com.muchq.json.utils;

import com.fasterxml.jackson.annotation.JsonCreator;
import com.fasterxml.jackson.annotation.JsonProperty;

import java.util.Optional;

public class Java8Widget {
  private final Optional<Integer> foo;

  @JsonCreator
  public Java8Widget(@JsonProperty("foo") Integer foo) {
    this.foo = Optional.ofNullable(foo);
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
    return other instanceof Java8Widget && ((Java8Widget) other).getFoo().equals(foo);
  }
}
