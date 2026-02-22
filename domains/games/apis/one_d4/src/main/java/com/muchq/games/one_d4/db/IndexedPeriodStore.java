package com.muchq.games.one_d4.db;

import java.time.Instant;
import java.util.Optional;

public interface IndexedPeriodStore {

  /**
   * Returns the indexed period for (player, platform, month) only if it is complete (fetched after
   * the month ended). Used to skip re-fetching games for already-indexed periods.
   */
  Optional<IndexedPeriod> findCompletePeriod(String player, String platform, String month);

  void upsertPeriod(
      String player,
      String platform,
      String month,
      Instant fetchedAt,
      boolean isComplete,
      int gamesCount);

  /** Month is stored as "YYYY-MM" in column year_month. */
  record IndexedPeriod(
      String player,
      String platform,
      String month,
      Instant fetchedAt,
      boolean isComplete,
      int gamesCount) {}
}
