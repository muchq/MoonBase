package com.muchq.games.one_d4.api;

import static org.assertj.core.api.Assertions.assertThat;
import static org.assertj.core.api.Assertions.assertThatCode;

import com.muchq.games.chessql.compiler.SqlCompiler;
import com.muchq.games.one_d4.api.dto.GameFeature;
import com.muchq.games.one_d4.db.StatementTimeouts;
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
    cache =
        new FirstPageCache(
            new MutableTicker(),
            FirstPageCache.MAX_AGE,
            new QueryExecutor(store, new SqlCompiler()));
    warmer = new FirstPageWarmer(cache);
  }

  @Test
  public void refresh_populatesTheCacheWithoutWaitingForARequest() {
    store.setQueryResult(List.of(gameFeature("https://chess.com/game/warm")));
    store.setOccurrencesResult(Map.of());

    warmer.refresh();

    assertThat(cache.peek()).isPresent();
    assertThat(cache.peek().orElseThrow().games().get(0).gameUrl())
        .isEqualTo("https://chess.com/game/warm");
  }

  @Test
  public void refresh_asksTheStoreForExactlyTheDefaultFirstPage() {
    store.setQueryResult(List.of());
    store.setOccurrencesResult(Map.of());

    warmer.refresh();

    // A warmer that warms any other page poisons the cache the controller serves as page 0.
    // The compiled query is pinned too: limit/offset alone would let the loader run a different
    // ChessQL query with the right pagination and stay green.
    assertThat(store.lastLimit()).isEqualTo(FirstPageCache.DEFAULT_LIMIT);
    assertThat(store.lastOffset()).isEqualTo(0);
    assertThat(store.lastCompiled())
        .isEqualTo(
            new SqlCompiler()
                .compile(
                    com.muchq.games.chessql.parser.Parser.parse(FirstPageCache.DEFAULT_QUERY),
                    null));
  }

  @Test
  public void refresh_replacesTheEarlierSnapshotWithNewData() {
    store.setQueryResult(List.of(gameFeature("https://chess.com/game/old")));
    warmer.refresh();

    store.setQueryResult(List.of(gameFeature("https://chess.com/game/new")));
    warmer.refresh();

    assertThat(cache.peek().orElseThrow().games().get(0).gameUrl())
        .isEqualTo("https://chess.com/game/new");
  }

  @Test
  public void refresh_swallowsStoreFailuresSoTheSchedulerKeepsTicking() {
    store.failQueriesWith(new RuntimeException("db down"));

    assertThatCode(() -> warmer.refresh()).doesNotThrowAnyException();
    assertThat(cache.peek()).as("a failed refresh must not poison the cache").isEmpty();
  }

  @Test
  public void refresh_afterAFailure_theEarlierSnapshotSurvives() {
    store.setQueryResult(List.of(gameFeature("https://chess.com/game/kept")));
    warmer.refresh();

    store.failQueriesWith(new RuntimeException("db down"));
    warmer.refresh();

    assertThat(cache.peek().orElseThrow().games().get(0).gameUrl())
        .as("a failed refresh keeps the last good snapshot until MAX_AGE expires it")
        .isEqualTo("https://chess.com/game/kept");
  }

  @Test
  public void scheduledDelayStaysWithinHalfOfMaxAge() throws Exception {
    io.micronaut.scheduling.annotation.Scheduled scheduled =
        FirstPageWarmer.class
            .getMethod("refresh")
            .getAnnotation(io.micronaut.scheduling.annotation.Scheduled.class);

    assertThat(scheduled).as("refresh() is no longer scheduled").isNotNull();
    assertThat(scheduled.fixedDelay()).matches("\\d+s");
    long delaySeconds = Long.parseLong(scheduled.fixedDelay().replace("s", ""));
    // The comment on the annotation states this invariant; this is what enforces it. If the
    // delay exceeds half MAX_AGE, the snapshot expires between refreshes and every first load
    // falls through to the database again.
    assertThat(delaySeconds * 2).isLessThanOrEqualTo(FirstPageCache.MAX_AGE.toSeconds());

    // The full tick budget: fixedDelay measures from completion, and a tick runs two reads each
    // bounded by the DAO's read timeout — so a worst-case-but-successful tick must still land a
    // fresh snapshot before the previous one expires. This is the arithmetic that makes the read
    // timeout's value safe, pinned here rather than reasoned about in javadoc.
    long worstTickSeconds = 2 * StatementTimeouts.SERVING_READ_SECONDS;
    assertThat(delaySeconds + worstTickSeconds)
        .as("a worst-case tick plus the delay must refresh before MAX_AGE expires the snapshot")
        .isLessThan(FirstPageCache.MAX_AGE.toSeconds());
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
