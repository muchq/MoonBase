package com.muchq.stecky;

import java.util.Objects;

public class Mapping {
  private final Class<?> handler;
  private final String path;

  private Mapping(Class<?> handler, String path) {
    this.handler = handler;
    this.path = path;
  }

  public Class<?> getHandler() {
    return handler;
  }

  public String getPath() {
    return path;
  }

  public static Mapping of(Class<?> handler, String path) {
    return new Mapping(handler, path);
  }

  @Override
  public boolean equals(Object o) {
    if (this == o) return true;
    if (o == null || getClass() != o.getClass()) return false;
    Mapping mapping = (Mapping) o;
    return Objects.equals(getHandler(), mapping.getHandler()) &&
        Objects.equals(getPath(), mapping.getPath());
  }

  @Override
  public int hashCode() {
    return Objects.hash(getHandler(), getPath());
  }
}
