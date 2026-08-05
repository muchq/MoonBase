package com.muchq.games.one_d4.api;

import static org.assertj.core.api.Assertions.assertThat;
import static org.assertj.core.api.Assertions.assertThatCode;

import com.muchq.games.chessql.compiler.SqlCompiler;
import com.muchq.games.one_d4.api.dto.GameFeature;
import java.time.Instant;
import java.util.List;
import java.util.Map;
import java.util.UUID;
import org.junit.jupiter.api.BeforeEach;
import org.junit.jupiter.api.Test;

public class FirstPageWarmerTest {

  private FakeGameFeatureStore store;
  private FirstPageCache cache;
  private FirstPageWarmer warmer;

  @BeforeEach
  public void setUp() {
    store = new FakeGameFeatureStore();
    cache = new FirstPageCache(new MutableClock(Instant.parse("2026-08-01T00:00:00Z")));
    warmer = new FirstPageWarmer(new QueryExecutor(store, new SqlCompiler()), cache);
  }

  @Test
  public void refresh_populatesTheCache() {
    store.setQueryResult(List.of(gameFeature("https://chess.com/game/warm")));
    store.setOccurrencesResult(Map.of());

    warmer.refresh();

    assertThat(cache.get()).isPresent();
    assertThat(cache.get().orElseThrow().games().get(0).gameUrl())
        .isEqualTo("https://chess.com/game/warm");
  }

  @Test
  public void refresh_replacesTheEarlierSnapshotWithNewData() {
    store.setQueryResult(List.of(gameFeature("https://chess.com/game/old")));
    warmer.refresh();

    store.setQueryResult(List.of(gameFeature("https://chess.com/game/new")));
    warmer.refresh();

    assertThat(cache.get().orElseThrow().games().get(0).gameUrl())
        .isEqualTo("https://chess.com/game/new");
  }

  @Test
  public void refresh_swallowsStoreFailuresSoTheSchedulerKeepsTicking() {
    store.failQueriesWith(new RuntimeException("db down"));

    assertThatCode(() -> warmer.refresh()).doesNotThrowAnyException();
    assertThat(cache.get()).as("a failed refresh must not poison the cache").isEmpty();
  }

  @Test
  public void refresh_afterAFailure_theEarlierSnapshotSurvives() {
    store.setQueryResult(List.of(gameFeature("https://chess.com/game/kept")));
    warmer.refresh();

    store.failQueriesWith(new RuntimeException("db down"));
    warmer.refresh();

    assertThat(cache.get().orElseThrow().games().get(0).gameUrl())
        .as("a failed refresh keeps the last good snapshot until MAX_AGE expires it")
        .isEqualTo("https://chess.com/game/kept");
  }

  private static GameFeature gameFeature(String gameUrl) {
    return new GameFeature(
        UUID.randomUUID(),
        UUID.randomUUID(),
        gameUrl,
        "CHESS_COM",
        "white",
        "black",
        2000,
        1900,
        null,
        "GM",
        "blitz",
        "B90",
        "Sicilian Defense Najdorf Variation",
        "Sicilian Defense",
        "1-0",
        Instant.now(),
        30,
        Instant.now(),
        "pgn");
  }
}
