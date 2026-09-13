package com.muchq.games.one_d4.testing;

import com.muchq.games.one_d4.db.IndexedPeriodStore;
import java.time.Instant;
import java.util.ArrayList;
import java.util.Collection;
import java.util.List;
import java.util.Optional;
import org.jspecify.annotations.Nullable;

/**
 * The one {@link IndexedPeriodStore} double (#1534). Stores rows for real and answers every read
 * from them — nothing here takes a compiled query, so there is nothing to program.
 *
 * <p>Keyed by (player, platform, month, excludeBullet), like the table's unique constraint: an
 * upsert on the same key replaces rather than stacks, and a month indexed without bullet games
 * answers nothing about the same month indexed with them.
 */
public final class FakeIndexedPeriodStore implements IndexedPeriodStore {

  private final List<IndexedPeriod> stored = new ArrayList<>();
  private int lookupCount;
  private @Nullable Collection<String> lastPlayersQueried;

  /** A complete period. Argument order follows {@link IndexedPeriod}'s own. */
  public void add(
      String player, String platform, String month, boolean excludeBullet, Instant fetchedAt) {
    add(player, platform, month, excludeBullet, fetchedAt, true);
  }

  public void add(
      String player,
      String platform,
      String month,
      boolean excludeBullet,
      Instant fetchedAt,
      boolean isComplete) {
    upsertPeriod(player, platform, month, fetchedAt, isComplete, 1, excludeBullet);
  }

  /** How many times the store was asked. One page of requests should cost one query. */
  public int lookupCount() {
    return lookupCount;
  }

  public @Nullable Collection<String> lastPlayersQueried() {
    return lastPlayersQueried;
  }

  @Override
  public List<IndexedPeriod> findPeriodsForPlayers(Collection<String> players) {
    lookupCount++;
    lastPlayersQueried = players;
    return stored.stream().filter(p -> players.contains(p.player())).toList();
  }

  @Override
  public Optional<IndexedPeriod> findCompletePeriod(
      String player, String platform, String month, boolean excludeBullet) {
    return stored.stream()
        .filter(p -> matches(p, player, platform, month, excludeBullet))
        .filter(IndexedPeriod::isComplete)
        .findFirst();
  }

  @Override
  public void upsertPeriod(
      String player,
      String platform,
      String month,
      Instant fetchedAt,
      boolean isComplete,
      int gamesCount,
      boolean excludeBullet) {
    stored.removeIf(p -> matches(p, player, platform, month, excludeBullet));
    stored.add(
        new IndexedPeriod(
            player, platform, month, fetchedAt, isComplete, gamesCount, excludeBullet));
  }

  @Override
  public int deleteOlderThan(Instant threshold) {
    int before = stored.size();
    stored.removeIf(p -> p.fetchedAt().isBefore(threshold));
    return before - stored.size();
  }

  private static boolean matches(
      IndexedPeriod period, String player, String platform, String month, boolean excludeBullet) {
    return period.player().equals(player)
        && period.platform().equals(platform)
        && period.month().equals(month)
        && period.excludeBullet() == excludeBullet;
  }
}
