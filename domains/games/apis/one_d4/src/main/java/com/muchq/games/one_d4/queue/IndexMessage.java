package com.muchq.games.one_d4.queue;

import java.util.UUID;

public record IndexMessage(
    UUID requestId,
    String player,
    String platform,
    String startMonth,
    String endMonth,
    boolean excludeBullet,
    boolean skipCache) {

  /** Convenience constructor for the common case of cache-respecting requests. */
  public IndexMessage(
      UUID requestId,
      String player,
      String platform,
      String startMonth,
      String endMonth,
      boolean excludeBullet) {
    this(requestId, player, platform, startMonth, endMonth, excludeBullet, false);
  }
}
