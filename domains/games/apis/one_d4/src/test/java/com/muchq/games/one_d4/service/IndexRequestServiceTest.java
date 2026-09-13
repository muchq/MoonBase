package com.muchq.games.one_d4.service;

import static org.assertj.core.api.Assertions.assertThat;
import static org.assertj.core.api.Assertions.assertThatThrownBy;

import com.muchq.games.one_d4.api.dto.IndexResponse;
import com.muchq.games.one_d4.db.IndexingRequestStore;
import com.muchq.games.one_d4.testing.FakeIndexedPeriodStore;
import com.muchq.games.one_d4.testing.FakeIndexingRequestStore;
import java.time.Clock;
import java.time.Duration;
import java.time.Instant;
import java.time.ZoneOffset;
import java.util.List;
import java.util.Optional;
import java.util.UUID;
import org.junit.jupiter.api.BeforeEach;
import org.junit.jupiter.api.Test;

public class IndexRequestServiceTest {

  private static final Instant NOW = Instant.parse("2026-07-01T12:00:00Z");

  private FakeIndexingRequestStore requestStore;
  private IndexRequestService service;

  @BeforeEach
  public void setUp() {
    requestStore = new FakeIndexingRequestStore(NOW);
    service = new IndexRequestService(requestStore, noPeriods(), Clock.fixed(NOW, ZoneOffset.UTC));
  }

  /** No period rows: availability resolves to EXPIRED, which these tests don't assert on. */
  private static DataAvailabilityResolver noPeriods() {
    return new DataAvailabilityResolver(new FakeIndexedPeriodStore());
  }

  private static IndexRequestService.Submission submission(String player, String platform) {
    return new IndexRequestService.Submission(player, platform, "2024-01", "2024-03", false, false);
  }

  /** The row is the dispatch: nothing in this JVM runs the work (#1389 phase 7). */
  @Test
  public void submit_createsTheRowAndReturnsPending() {
    IndexResponse response = service.submit(submission("hikaru", "CHESS_COM"));

    assertThat(response.status()).isEqualTo("PENDING");
    assertThat(response.player()).isEqualTo("hikaru");
    assertThat(requestStore.listRecent(10)).hasSize(1);
  }

  @Test
  public void submit_normalizesPlayerToLowercaseForDedupeAndCacheKeys() {
    IndexResponse first = service.submit(submission("Hikaru", "CHESS_COM"));
    IndexResponse second = service.submit(submission("HIKARU", "chess.com"));

    assertThat(first.player()).isEqualTo("hikaru");
    // Same lowercased identity → deduped onto the existing PENDING request
    assertThat(second.id()).isEqualTo(first.id());
    assertThat(requestStore.listRecent(10)).hasSize(1);
  }

  @Test
  public void submit_acceptsChessComPlatformSpelling() {
    IndexResponse response = service.submit(submission("hikaru", "chess.com"));
    assertThat(response.platform()).isEqualTo("CHESS_COM");
    assertThat(requestStore.listRecent(10).get(0).platform()).isEqualTo("CHESS_COM");
  }

  /**
   * The gate #1527 was waiting on. Every spelling a user might type reaches the one canonical form
   * the worker's archive registry is keyed on — anything else fails the run rather than completing
   * it empty, so what this accepts and what that registry serves have to be the same set.
   */
  @Test
  public void submit_acceptsLichessHoweverItIsSpelled() {
    for (String spelling : List.of("lichess", "LICHESS", "Lichess", " lichess ")) {
      IndexResponse response = service.submit(submission("player-" + spelling.strip(), spelling));
      assertThat(response.platform()).as(spelling).isEqualTo("LICHESS");
    }
    assertThat(requestStore.listRecent(10))
        .extracting(IndexingRequestStore.IndexingRequest::platform)
        .containsOnly("LICHESS");
  }

  @Test
  public void submit_stillRefusesAPlatformNobodyIndexes() {
    assertThatThrownBy(() -> service.submit(submission("x", "chess24.com")))
        .isInstanceOf(IllegalArgumentException.class)
        .hasMessageContaining("Unsupported platform")
        // The message names what it will take, so a 400 is actionable without the source.
        .hasMessageContaining("CHESS_COM")
        .hasMessageContaining("LICHESS");
  }

  @Test
  public void submit_skipCachePropagatesTheFlagWhenNothingIsInFlight() {
    IndexResponse forced =
        service.submit(
            new IndexRequestService.Submission(
                "hikaru", "CHESS_COM", "2024-01", "2024-03", false, true));

    assertThat(requestStore.listRecent(10)).hasSize(1);
    assertThat(requestStore.findById(forced.id()).orElseThrow().skipCache()).isTrue();
    assertThat(forced.status()).isEqualTo("PENDING");
  }

  /**
   * skipCache forces a refetch; it does not license a second concurrent run of the same range. Two
   * indexers over one set of games interleave the occurrence delete/insert and double every motif
   * count, so the forced submit coalesces onto the in-flight request instead.
   */
  @Test
  public void submit_skipCacheDoesNotStartARivalRunWhileOneIsInFlight() {
    IndexResponse first = service.submit(submission("hikaru", "CHESS_COM"));

    IndexResponse forced =
        service.submit(
            new IndexRequestService.Submission(
                "hikaru", "CHESS_COM", "2024-01", "2024-03", false, true));

    assertThat(requestStore.listRecent(10)).hasSize(1);
    assertThat(forced.id()).isEqualTo(first.id());
    assertThat(forced.status()).isEqualTo("PENDING");
  }

  /** Once the in-flight run reaches a terminal status the force works normally. */
  @Test
  public void submit_skipCacheForcesAFreshRunOnceTheInFlightOneFinishes() {
    IndexResponse first = service.submit(submission("hikaru", "CHESS_COM"));
    requestStore.updateStatus(first.id(), "COMPLETED", null, 30);

    IndexResponse forced =
        service.submit(
            new IndexRequestService.Submission(
                "hikaru", "CHESS_COM", "2024-01", "2024-03", false, true));

    assertThat(requestStore.listRecent(10)).hasSize(2);
    assertThat(requestStore.findById(forced.id()).orElseThrow().skipCache()).isTrue();
    assertThat(forced.id()).isNotEqualTo(first.id());
  }

  /**
   * #1249: two callers that both see nothing must still produce one unit of work. Only the claim
   * can decide that — a read followed by a write has a window in it, which is why the submit path
   * asks createOrAdopt and nothing else.
   */
  @Test
  public void submit_secondCallerAdoptsRatherThanDispatchingASecondTime() {
    IndexResponse first = service.submit(submission("hikaru", "CHESS_COM"));
    IndexResponse second = service.submit(submission("hikaru", "CHESS_COM"));

    assertThat(second.id()).isEqualTo(first.id());
    assertThat(requestStore.listRecent(10)).hasSize(1);
  }

  /**
   * #1250: a request nothing is going to run must not hold its range hostage. Before the age bound,
   * that row answered every later submit forever and the only escape was skipCache.
   *
   * <p>No worker has ever held a lease in this fixture, which since #1279 is the whole reason it
   * retires: an old row is only stalled when nothing is running anywhere. An old row on a working
   * fleet is a backlog, and the test below it covers the case where the age has not yet elapsed.
   *
   * <p>This is also what pins the submit path to a single call. #1279 took the clock out of {@code
   * findExistingRequest}, and the dedupe read that used to run ahead of {@code createOrAdopt}
   * immediately started answering with this row instead of reaching the reclaim that retires it —
   * reintroducing a short-circuit here fails on exactly this assertion.
   */
  @Test
  public void submit_strandedRequestIsRetiredAndReplacedOnTheNextSubmit() {
    IndexResponse stranded = service.submit(submission("hikaru", "CHESS_COM"));
    requestStore.strand(stranded.id(), NOW.minus(Duration.ofHours(6)));

    IndexResponse replacement = service.submit(submission("hikaru", "CHESS_COM"));

    assertThat(replacement.id()).isNotEqualTo(stranded.id());
    assertThat(replacement.status()).isEqualTo("PENDING");
    assertThat(requestStore.listRecent(10)).hasSize(2);
    assertThat(requestStore.findById(stranded.id()).orElseThrow().status()).isEqualTo("FAILED");
  }

  @Test
  public void submit_aFreshInFlightRequestIsStillReusedNotReplaced() {
    IndexResponse first = service.submit(submission("hikaru", "CHESS_COM"));
    requestStore.strand(first.id(), NOW.minus(Duration.ofMinutes(20)));

    IndexResponse second = service.submit(submission("hikaru", "CHESS_COM"));

    assertThat(second.id()).isEqualTo(first.id());
    assertThat(requestStore.listRecent(10)).hasSize(1);
  }

  @Test
  public void submit_acceptsFullTwelveMonthRange() {
    IndexResponse response =
        service.submit(
            new IndexRequestService.Submission(
                "hikaru", "CHESS_COM", "2024-01", "2024-12", false, false));

    assertThat(response.status()).isEqualTo("PENDING");
    assertThat(requestStore.listRecent(10)).hasSize(1);
  }

  @Test
  public void validationRejectsNullFields() {
    assertThatThrownBy(() -> service.submit(submission(null, "CHESS_COM")))
        .isInstanceOf(IllegalArgumentException.class)
        .hasMessageContaining("player is required");
    assertThatThrownBy(() -> service.submit(submission("x", null)))
        .isInstanceOf(IllegalArgumentException.class)
        .hasMessageContaining("platform is required");
    assertThatThrownBy(
            () ->
                service.submit(
                    new IndexRequestService.Submission(
                        "x", "CHESS_COM", null, "2024-01", false, false)))
        .isInstanceOf(IllegalArgumentException.class)
        .hasMessageContaining("startMonth is required");
    assertThatThrownBy(
            () ->
                service.submit(
                    new IndexRequestService.Submission(
                        "x", "CHESS_COM", "2024-01", null, false, false)))
        .isInstanceOf(IllegalArgumentException.class)
        .hasMessageContaining("endMonth is required");
    assertThatThrownBy(
            () ->
                service.submit(
                    new IndexRequestService.Submission(
                        "x", "CHESS_COM", "2024-01", "June", false, false)))
        .isInstanceOf(IllegalArgumentException.class)
        .hasMessageContaining("endMonth must be in YYYY-MM format");
  }

  @Test
  public void validationRejectsBadInput() {
    assertThatThrownBy(() -> service.submit(submission(" ", "CHESS_COM")))
        .isInstanceOf(IllegalArgumentException.class)
        .hasMessageContaining("player is required");
    assertThatThrownBy(() -> service.submit(submission("x", "  ")))
        .isInstanceOf(IllegalArgumentException.class)
        .hasMessageContaining("platform is required");
    assertThatThrownBy(
            () ->
                service.submit(
                    new IndexRequestService.Submission(
                        "x", "CHESS_COM", "January", "2024-01", false, false)))
        .isInstanceOf(IllegalArgumentException.class)
        .hasMessageContaining("YYYY-MM");
    assertThatThrownBy(
            () ->
                service.submit(
                    new IndexRequestService.Submission(
                        "x", "CHESS_COM", "2024-05", "2024-01", false, false)))
        .isInstanceOf(IllegalArgumentException.class)
        .hasMessageContaining("must not be after");
    assertThatThrownBy(
            () ->
                service.submit(
                    new IndexRequestService.Submission(
                        "x", "CHESS_COM", "2023-01", "2024-06", false, false)))
        .isInstanceOf(IllegalArgumentException.class)
        .hasMessageContaining("Maximum range is 12 months");
  }

  @Test
  public void status_mapsStoredRequest() {
    IndexResponse created = service.submit(submission("hikaru", "CHESS_COM"));
    requestStore.updateStatus(created.id(), "PROCESSING", null, 7);

    Optional<IndexResponse> status = service.status(created.id());

    assertThat(status).isPresent();
    assertThat(status.get().status()).isEqualTo("PROCESSING");
    assertThat(status.get().gamesIndexed()).isEqualTo(7);
    assertThat(service.status(UUID.randomUUID())).isEmpty();
  }
}
