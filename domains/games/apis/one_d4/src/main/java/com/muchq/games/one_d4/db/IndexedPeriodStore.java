package com.muchq.games.one_d4.db;

import java.time.Instant;
import java.util.Optional;

public interface IndexedPeriodStore {

  /**
   * Returns the indexed period for (player, platform, month, excludeBullet) only if it is complete
   * (fetched after the month ended). excludeBullet is part of the key so that a request with
   * different filtering settings re-fetches rather than reusing an incompatible cached period.
   */
  Optional<IndexedPeriod> findCompletePeriod(
      String player, String platform, String month, boolean excludeBullet);

  void upsertPeriod(
      String player,
      String platform,
      String month,
      Instant fetchedAt,
      boolean isComplete,
      int gamesCount,
      boolean excludeBullet);

  int deleteOlderThan(Instant threshold);

  /** Month is stored as "YYYY-MM" in column year_month. */
  record IndexedPeriod(
      String player,
      String platform,
      String month,
      Instant fetchedAt,
      boolean isComplete,
      int gamesCount,
      boolean excludeBullet) {}
}
