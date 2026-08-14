package com.muchq.games.one_d4.db;

import static org.assertj.core.api.Assertions.assertThat;

import java.time.Instant;
import java.util.List;
import org.junit.jupiter.api.BeforeEach;
import org.junit.jupiter.api.Test;

public class IndexedPeriodDaoTest {

  private IndexedPeriodDao dao;

  @BeforeEach
  public void setUp() {
    dao = new IndexedPeriodDao(TestDb.create("period_test").jdbi(), H2SqlDialect.INSTANCE);
  }

  @Test
  public void findCompletePeriod_returnsEmptyWhenNone() {
    assertThat(dao.findCompletePeriod("p", "CHESS_COM", "2024-01", false)).isEmpty();
  }

  @Test
  public void upsertAndFindCompletePeriod_returnsPeriodWhenComplete() {
    Instant fetchedAt = Instant.parse("2024-02-15T12:00:00Z");
    dao.upsertPeriod("player1", "CHESS_COM", "2024-01", fetchedAt, true, 42, false);
    var result = dao.findCompletePeriod("player1", "CHESS_COM", "2024-01", false);
    assertThat(result).isPresent();
    assertThat(result.get().player()).isEqualTo("player1");
    assertThat(result.get().platform()).isEqualTo("CHESS_COM");
    assertThat(result.get().month()).isEqualTo("2024-01");
    assertThat(result.get().gamesCount()).isEqualTo(42);
    assertThat(result.get().isComplete()).isTrue();
    assertThat(result.get().excludeBullet()).isFalse();
  }

  @Test
  public void findCompletePeriod_returnsEmptyWhenIncomplete() {
    Instant fetchedAt = Instant.parse("2024-01-15T12:00:00Z");
    dao.upsertPeriod("player2", "CHESS_COM", "2024-01", fetchedAt, false, 10, false);
    assertThat(dao.findCompletePeriod("player2", "CHESS_COM", "2024-01", false)).isEmpty();
  }

  @Test
  public void upsertOverwritesExistingPeriod() {
    dao.upsertPeriod("player3", "CHESS_COM", "2024-03", Instant.EPOCH, true, 1, false);
    dao.upsertPeriod(
        "player3", "CHESS_COM", "2024-03", Instant.parse("2024-04-01T00:00:00Z"), true, 99, false);
    var result = dao.findCompletePeriod("player3", "CHESS_COM", "2024-03", false);
    assertThat(result).isPresent();
    assertThat(result.get().gamesCount()).isEqualTo(99);
  }

  @Test
  public void findCompletePeriod_distinguishesByExcludeBullet() {
    Instant fetchedAt = Instant.parse("2024-02-01T00:00:00Z");
    dao.upsertPeriod("player4", "CHESS_COM", "2024-01", fetchedAt, true, 10, false);
    dao.upsertPeriod("player4", "CHESS_COM", "2024-01", fetchedAt, true, 7, true);

    var withBullet = dao.findCompletePeriod("player4", "CHESS_COM", "2024-01", false);
    assertThat(withBullet).isPresent();
    assertThat(withBullet.get().gamesCount()).isEqualTo(10);

    var withoutBullet = dao.findCompletePeriod("player4", "CHESS_COM", "2024-01", true);
    assertThat(withoutBullet).isPresent();
    assertThat(withoutBullet.get().gamesCount()).isEqualTo(7);
  }

  @Test
  public void findPeriodsForPlayers_returnsEveryVariantForTheNamedPlayersOnly() {
    Instant fetchedAt = Instant.parse("2024-02-01T00:00:00Z");
    dao.upsertPeriod("wanted", "CHESS_COM", "2024-01", fetchedAt, true, 10, false);
    dao.upsertPeriod("wanted", "CHESS_COM", "2024-02", fetchedAt, true, 11, false);
    // Same player and month, different bullet setting: a distinct row the caller must be able
    // to tell apart, since it indexes a different corpus.
    dao.upsertPeriod("wanted", "CHESS_COM", "2024-01", fetchedAt, true, 7, true);
    dao.upsertPeriod("other", "CHESS_COM", "2024-01", fetchedAt, true, 99, false);

    var found = dao.findPeriodsForPlayers(List.of("wanted"));

    assertThat(found).hasSize(3).allSatisfy(p -> assertThat(p.player()).isEqualTo("wanted"));
    assertThat(found)
        .extracting(p -> p.month() + "/" + p.excludeBullet())
        .containsExactlyInAnyOrder("2024-01/false", "2024-02/false", "2024-01/true");
    assertThat(found).extracting(IndexedPeriodStore.IndexedPeriod::fetchedAt).contains(fetchedAt);
  }

  @Test
  public void findPeriodsForPlayers_handlesSeveralPlayersInOneQuery() {
    Instant fetchedAt = Instant.parse("2024-02-01T00:00:00Z");
    dao.upsertPeriod("a", "CHESS_COM", "2024-01", fetchedAt, true, 1, false);
    dao.upsertPeriod("b", "CHESS_COM", "2024-01", fetchedAt, true, 2, false);
    dao.upsertPeriod("c", "CHESS_COM", "2024-01", fetchedAt, true, 3, false);

    assertThat(dao.findPeriodsForPlayers(List.of("a", "c")))
        .extracting(IndexedPeriodStore.IndexedPeriod::player)
        .containsExactlyInAnyOrder("a", "c");
  }

  @Test
  public void findPeriodsForPlayers_withNoPlayersReturnsEmptyWithoutQuerying() {
    dao.upsertPeriod(
        "a", "CHESS_COM", "2024-01", Instant.parse("2024-02-01T00:00:00Z"), true, 1, false);

    // An empty IN (...) list is a SQL syntax error, so the short-circuit is load-bearing.
    assertThat(dao.findPeriodsForPlayers(List.of())).isEmpty();
  }

  @Test
  public void findPeriodsForPlayers_doesNotSeeSweptRows() {
    Instant old = Instant.parse("2024-01-01T00:00:00Z");
    dao.upsertPeriod("swept", "CHESS_COM", "2024-01", old, true, 5, false);
    assertThat(dao.findPeriodsForPlayers(List.of("swept"))).hasSize(1);

    dao.deleteOlderThan(Instant.parse("2024-02-01T00:00:00Z"));

    assertThat(dao.findPeriodsForPlayers(List.of("swept"))).isEmpty();
  }
}
