package com.muchq.games.one_d4.api.dto;

public record IndexRequest(
    String player, String platform, String startMonth, String endMonth, Boolean includeBullet) {
  public IndexRequest {
    if (includeBullet == null) {
      includeBullet = false;
    }
  }
}
