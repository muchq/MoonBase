package com.muchq.games.one_d4.testing;

import static org.assertj.core.api.Assertions.assertThat;
import static org.assertj.core.api.Assertions.assertThatThrownBy;

import com.muchq.games.one_d4.api.dto.AggregateRow;
import com.muchq.games.one_d4.api.dto.GameFeature;
import com.muchq.games.one_d4.api.dto.OccurrenceRow;
import com.muchq.games.one_d4.db.GameFeatureStore.AggregateTotals;
import com.muchq.games.one_d4.db.GameFeatureStore.GameOpening;
import java.time.Instant;
import java.util.List;
import java.util.Map;
import java.util.UUID;
import org.junit.jupiter.api.Test;

/**
 * Nine test sources read this fake, so a wrong answer here is a false green in all of them rather
 * than one failure. These pin the parts they rely on.
 */
class FakeGameFeatureStoreTest {

  private final FakeGameFeatureStore store = new FakeGameFeatureStore();

  // The trap in merging the three doubles this replaces: two of them called their recorder
  // "lastCompiled", one written by query() and one by aggregate(). Shared, an aggregate
  // assertion would pass on an argument query() recorded.
  @Test
  void queryAndAggregateRecordIntoSeparateSlots() {
    store.query("compiled-query", 10, 20);
    store.aggregate("compiled-aggregate", List.of("eco"), true, 30);

    assertThat(store.lastQueryCompiled()).isEqualTo("compiled-query");
    assertThat(store.lastQueryLimit()).isEqualTo(10);
    assertThat(store.lastQueryOffset()).isEqualTo(20);
    assertThat(store.lastAggregateCompiled()).isEqualTo("compiled-aggregate");
    assertThat(store.lastAggregateLimit()).isEqualTo(30);
    assertThat(store.lastAggregateGroupColumns()).containsExactly("eco");
    assertThat(store.lastAggregateOutcomeMetrics()).isTrue();
  }

  // A caller that has asked nothing must be distinguishable from one that asked for null.
  @Test
  void theRecordersATestReadsStartUnset() {
    assertThat(store.lastQueryCompiled()).isNull();
    assertThat(store.lastAggregateCompiled()).isNull();
    assertThat(store.lastTotalsCompiled()).isNull();
    assertThat(store.lastAggregateGroupColumns()).isNull();
    assertThat(store.lastAggregateOutcomeMetrics()).isNull();
    assertThat(store.queryCount()).isZero();
    assertThat(store.totalsCalls()).isZero();
  }

  @Test
  void queryReturnsWhatWasProgrammed() {
    store.setQueryResult(List.of(game("a")));

    assertThat(store.query("q", 1, 0)).extracting(GameFeature::gameUrl).containsExactly("a");
  }

  /** Cache tests count loads, so a repeated query must not be silently collapsed. */
  @Test
  void queryCountsEveryCall() {
    store.query("q", 1, 0);
    store.query("q", 1, 0);

    assertThat(store.queryCount()).isEqualTo(2);
  }

  @Test
  void queryRunsTheHookBeforeReturning() {
    List<String> order = new java.util.ArrayList<>();
    store.setQueryResult(List.of(game("a")));
    store.onQuery(() -> order.add("hook"));

    store.query("q", 1, 0);
    order.add("returned");

    assertThat(order).containsExactly("hook", "returned");
  }

  // Recorded, then thrown. A fake that threw first would leave the call invisible, so a test
  // asking what the caller actually attempted before it failed could not be written at all.
  @Test
  void aFailedQueryIsStillRecorded() {
    store.failQueriesWith(new IllegalStateException("boom"));

    assertThatThrownBy(() -> store.query("q", 5, 0)).isInstanceOf(IllegalStateException.class);

    assertThat(store.queryCount()).isEqualTo(1);
    assertThat(store.lastQueryCompiled()).isEqualTo("q");
    assertThat(store.lastQueryLimit()).isEqualTo(5);
  }

  // Matches the DAO, which groups only the rows it found: a game with no occurrences is absent
  // from the map rather than present with an empty one.
  @Test
  void queryOccurrencesOmitsUrlsThatHaveNone() {
    OccurrenceRow row =
        new OccurrenceRow("a", "FORK", 3, "w", "d", null, null, null, false, false, null);
    store.setOccurrences(Map.of("a", Map.of("FORK", List.of(row))));

    Map<String, Map<String, List<OccurrenceRow>>> found = store.queryOccurrences(List.of("a", "b"));

    assertThat(found).containsOnlyKeys("a");
    assertThat(found.get("a")).containsOnlyKeys("FORK");
  }

  @Test
  void aggregateAndTotalsReturnWhatWasProgrammed() {
    AggregateRow row = new AggregateRow(Map.of("eco", "B00"), 7);
    store.setAggregateRows(List.of(row));
    store.setAggregateTotals(new AggregateTotals(70, 3));

    assertThat(store.aggregate("q", List.of("eco"), false, 10)).containsExactly(row);
    assertThat(store.aggregateTotals("q")).isEqualTo(new AggregateTotals(70, 3));
  }

  /** One controller call must not become two round trips to the database. */
  @Test
  void aggregateTotalsCountsItsCalls() {
    store.aggregateTotals("q");
    store.aggregateTotals("q");

    assertThat(store.totalsCalls()).isEqualTo(2);
    assertThat(store.lastTotalsCompiled()).isEqualTo("q");
  }

  @Test
  void fetchOpeningsForRederivePagesWithoutOverlapOrGaps() {
    for (int i = 0; i < 5; i++) {
      store.addOpening("g" + i, "Some Opening", "stale");
    }

    assertThat(store.fetchOpeningsForRederive(2, 0))
        .extracting(GameOpening::gameUrl)
        .containsExactly("g0", "g1");
    assertThat(store.fetchOpeningsForRederive(2, 2))
        .extracting(GameOpening::gameUrl)
        .containsExactly("g2", "g3");
    assertThat(store.fetchOpeningsForRederive(2, 4))
        .extracting(GameOpening::gameUrl)
        .containsExactly("g4");
    assertThat(store.fetchOpeningsForRederive(2, 6)).isEmpty();
  }

  // The production method writes only opening_family, conditioned on the name it derived from.
  // A fake that rewrote the name too would hide a caller passing the wrong one.
  @Test
  void updateOpeningFamiliesWritesTheFamilyAndLeavesTheNameAlone() {
    store.addOpening("g0", "Owens Defense", "stale");

    int updated = store.updateOpeningFamilies(List.of(new GameOpening("g0", "ignored", "Owens")));

    assertThat(updated).isEqualTo(1);
    assertThat(store.familyOf("g0")).isEqualTo("Owens");
    assertThat(store.fetchOpeningsForRederive(1, 0).get(0).openingName())
        .isEqualTo("Owens Defense");
  }

  /** Paging bugs show up as a url written twice, so every write is recorded in order. */
  @Test
  void updateOpeningFamiliesRecordsEveryWrittenUrl() {
    store.addOpening("g0", "n", "stale");
    store.addOpening("g1", "n", "stale");

    store.updateOpeningFamilies(List.of(new GameOpening("g0", "n", "a")));
    store.updateOpeningFamilies(List.of(new GameOpening("g1", "n", "b")));

    assertThat(store.writtenUrls()).containsExactly("g0", "g1");
  }

  @Test
  void updateOpeningFamiliesIgnoresAGameItDoesNotHold() {
    store.addOpening("g0", "n", "stale");

    store.updateOpeningFamilies(List.of(new GameOpening("absent", "n", "a")));

    assertThat(store.familyOf("g0")).isEqualTo("stale");
  }

  @Test
  void clearRecordedCallsForgetsTheQueryAndAggregateCallsAndKeepsTheProgramming() {
    AggregateRow row = new AggregateRow(Map.of("eco", "B00"), 7);
    store.setAggregateRows(List.of(row));
    store.setQueryResult(List.of(game("a")));
    store.query("q", 1, 0);
    store.aggregate("q", List.of("eco"), true, 10);
    store.aggregateTotals("q");

    store.clearRecordedCalls();

    assertThat(store.queryCount()).isZero();
    assertThat(store.lastQueryCompiled()).isNull();
    assertThat(store.lastAggregateCompiled()).isNull();
    assertThat(store.lastTotalsCompiled()).isNull();
    assertThat(store.totalsCalls()).isZero();
    assertThat(store.query("q", 1, 0)).hasSize(1);
    assertThat(store.aggregate("q", List.of("eco"), true, 10)).containsExactly(row);
  }

  // The write side is the worker's. deleteOlderThan is the one that matters: answering 0 would
  // let a retention test adopting this fake read "nothing swept" as a pass.
  @Test
  void theWriteSideIsUnsupportedRatherThanSilent() {
    assertThatThrownBy(() -> store.insertBatch(List.of()))
        .isInstanceOf(UnsupportedOperationException.class);
    assertThatThrownBy(() -> store.deleteOlderThan(Instant.EPOCH))
        .isInstanceOf(UnsupportedOperationException.class);
  }

  /** Only the worker flushes. A read-side test reaching this is asserting the wrong thing. */
  @Test
  void flushOwnedIsUnsupported() {
    assertThatThrownBy(
            () -> store.flushOwned(UUID.randomUUID(), "owner", Instant.EPOCH, List.of(), Map.of()))
        .isInstanceOf(UnsupportedOperationException.class);
  }

  private static GameFeature game(String url) {
    return new GameFeature(
        UUID.randomUUID(),
        UUID.randomUUID(),
        url,
        "CHESS_COM",
        "w",
        "b",
        1,
        1,
        null,
        null,
        "blitz",
        "B00",
        "n",
        "f",
        "1-0",
        Instant.EPOCH,
        40,
        Instant.EPOCH,
        "pgn");
  }
}
