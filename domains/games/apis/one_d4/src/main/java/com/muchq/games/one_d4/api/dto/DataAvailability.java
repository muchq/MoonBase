package com.muchq.games.one_d4.api.dto;

import java.time.Instant;
import org.jspecify.annotations.Nullable;

/**
 * Whether a completed index request's data is still stored, or has been swept by retention.
 *
 * @param status {@code AVAILABLE} (every month still indexed), {@code PARTIAL} (some months swept),
 *     {@code EXPIRED} (all swept), or {@code UNKNOWN} (the stored month range could not be read)
 * @param monthsAvailable months in the request's range that are still indexed
 * @param monthsTotal months the request covers
 * @param expiresAt when the first of the remaining months is due to be swept, or null when none
 *     remain; serialized as epoch seconds, matching {@code playedAt} elsewhere in this API
 */
public record DataAvailability(
    String status, int monthsAvailable, int monthsTotal, @Nullable Instant expiresAt) {

  public static DataAvailability unknown() {
    return new DataAvailability("UNKNOWN", 0, 0, null);
  }
}
