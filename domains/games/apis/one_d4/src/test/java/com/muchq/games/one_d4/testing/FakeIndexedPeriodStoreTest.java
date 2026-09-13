package com.muchq.games.one_d4.testing;

import static org.assertj.core.api.Assertions.assertThat;

import java.time.Instant;
import java.util.List;
import org.junit.jupiter.api.Test;

class FakeIndexedPeriodStoreTest {

  private static final Instant FETCHED = Instant.parse("2026-01-01T00:00:00Z");

  private final FakeIndexedPeriodStore store = new FakeIndexedPeriodStore();

  @Test
  void findPeriodsForPlayersReturnsOnlyTheRequestedPlayers() {
    store.add("alice", "CHESS_COM", "2025-01", false, FETCHED);
    store.add("bob", "CHESS_COM", "2025-01", false, FETCHED);

    assertThat(store.findPeriodsForPlayers(List.of("alice")))
        .extracting(p -> p.player())
        .containsExactly("alice");
  }

  /** Resolver tests assert one query serves a whole page, so the call has to be countable. */
  @Test
  void findPeriodsForPlayersRecordsWhatItWasAsked() {
    store.findPeriodsForPlayers(List.of("alice", "bob"));

    assertThat(store.lookupCount()).isEqualTo(1);
    assertThat(store.lastPlayersQueried()).containsExactly("alice", "bob");
  }

  @Test
  void whatUpsertPeriodWritesIsWhatFindCompletePeriodReads() {
    store.upsertPeriod("alice", "CHESS_COM", "2025-01", FETCHED, true, 42, false);

    assertThat(store.findCompletePeriod("alice", "CHESS_COM", "2025-01", false))
        .get()
        .extracting(p -> p.gamesCount())
        .isEqualTo(42);
  }

  // excludeBullet is part of the key: a month indexed without bullet answers nothing about the
  // same month indexed with it.
  @Test
  void findCompletePeriodIsKeyedByExcludeBullet() {
    store.upsertPeriod("alice", "CHESS_COM", "2025-01", FETCHED, true, 42, false);

    assertThat(store.findCompletePeriod("alice", "CHESS_COM", "2025-01", true)).isEmpty();
  }

  @Test
  void findCompletePeriodWithholdsAnIncompletePeriod() {
    store.add("alice", "CHESS_COM", "2025-01", false, FETCHED, false);

    assertThat(store.findCompletePeriod("alice", "CHESS_COM", "2025-01", false)).isEmpty();
    assertThat(store.findPeriodsForPlayers(List.of("alice"))).hasSize(1);
  }

  // The five-argument add seeds a *complete* period; the six-argument one is how a test asks for
  // the other kind. Without this, flipping the default turns the incomplete-period tests into
  // duplicates of their neighbours and nothing notices.
  @Test
  void theShortAddSeedsACompletePeriod() {
    store.add("alice", "CHESS_COM", "2025-01", false, FETCHED);

    assertThat(store.findCompletePeriod("alice", "CHESS_COM", "2025-01", false)).isPresent();
  }

  /** Re-upserting a period replaces it rather than stacking a second row on the same key. */
  @Test
  void upsertPeriodReplacesTheRowOnTheSameKey() {
    store.upsertPeriod("alice", "CHESS_COM", "2025-01", FETCHED, true, 1, false);
    store.upsertPeriod("alice", "CHESS_COM", "2025-01", FETCHED, true, 9, false);

    assertThat(store.findPeriodsForPlayers(List.of("alice"))).hasSize(1);
    assertThat(store.findCompletePeriod("alice", "CHESS_COM", "2025-01", false))
        .get()
        .extracting(p -> p.gamesCount())
        .isEqualTo(9);
  }
}
