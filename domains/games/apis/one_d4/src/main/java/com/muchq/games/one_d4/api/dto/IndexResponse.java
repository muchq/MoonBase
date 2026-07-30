package com.muchq.games.one_d4.api.dto;

import java.util.UUID;
import org.jspecify.annotations.Nullable;

/**
 * @param data whether the games this request produced are still stored, or have been swept by
 *     retention. Null when the request has not COMPLETED, and on the paths that report a freshly
 *     created request without consulting storage.
 */
public record IndexResponse(
    UUID id,
    String player,
    String platform,
    String startMonth,
    String endMonth,
    String status,
    int gamesIndexed,
    @Nullable String errorMessage,
    boolean excludeBullet,
    @Nullable DataAvailability data) {

  public IndexResponse(
      UUID id,
      String player,
      String platform,
      String startMonth,
      String endMonth,
      String status,
      int gamesIndexed,
      @Nullable String errorMessage,
      boolean excludeBullet) {
    this(
        id,
        player,
        platform,
        startMonth,
        endMonth,
        status,
        gamesIndexed,
        errorMessage,
        excludeBullet,
        null);
  }

  public IndexResponse withData(@Nullable DataAvailability data) {
    return new IndexResponse(
        id,
        player,
        platform,
        startMonth,
        endMonth,
        status,
        gamesIndexed,
        errorMessage,
        excludeBullet,
        data);
  }
}
