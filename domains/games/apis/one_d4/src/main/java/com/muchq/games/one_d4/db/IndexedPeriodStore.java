package com.muchq.games.one_d4.db;

import java.time.Instant;
import java.util.Collection;
import java.util.List;
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

  /**
   * Every surviving period row for these players, across platforms and both excludeBullet values.
   *
   * <p>This is how callers ask "is that data still there?". Retention deletes from this table on
   * the same clock it deletes games, and the row is keyed by exactly what an index request covers,
   * so a missing row means the period has been swept. Counting {@code game_features} by request_id
   * would not answer the same question: reindexing a period reassigns those rows to the newer
   * request, so an older request would report zero even while its games are still stored.
   *
   * <p>Filtering by platform and excludeBullet is left to the caller — a handful of players'
   * periods is a small result set, and one query serves a whole page of requests.
   */
  List<IndexedPeriod> findPeriodsForPlayers(Collection<String> players);

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
