package com.muchq.games.one_d4.api.dto;

public record IndexRequest(
    String player,
    String platform,
    String startMonth,
    String endMonth,
    Boolean excludeBullet,
    Boolean skipCache) {

  /** Convenience constructor for requests that respect the indexed-period cache. */
  public IndexRequest(
      String player, String platform, String startMonth, String endMonth, Boolean excludeBullet) {
    this(player, platform, startMonth, endMonth, excludeBullet, null);
  }
}
