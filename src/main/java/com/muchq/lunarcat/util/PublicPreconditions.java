package com.muchq.lunarcat.util;

import javax.ws.rs.BadRequestException;

public final class PublicPreconditions {
  private PublicPreconditions() {}

  public static void checkArgument(boolean condition, String message) {
    if (!condition) {
      throw new BadRequestException(message);
    }
  }

  public static <T> T checkNotNull(T arg, String message) {
    if (arg == null) {
      throw new BadRequestException(message);
    }
    return arg;
  }
}
