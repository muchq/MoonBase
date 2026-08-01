package com.muchq.games.one_d4.service;

import static org.assertj.core.api.Assertions.assertThat;

import com.muchq.games.one_d4.api.dto.DataAvailability;
import com.muchq.games.one_d4.db.IndexedPeriodStore;
import com.muchq.games.one_d4.db.IndexingRequestStore.IndexingRequest;
import com.muchq.games.one_d4.db.RetentionPolicy;
import java.time.Instant;
import java.util.ArrayList;
import java.util.Collection;
import java.util.List;
import java.util.Optional;
import java.util.UUID;
import org.junit.jupiter.api.Test;

/**
 * A request row in {@code indexing_requests} outlives the games it produced, so "COMPLETED, 325
 * games" keeps rendering long after retention has swept the data out from under it. These tests pin
 * the signal that closes that gap.
 */
public class DataAvailabilityResolverTest {

  private static final Instant FETCHED = Instant.parse("2026-07-20T12:00:00Z");

  private final FakePeriodStore periods = new FakePeriodStore();
  private final DataAvailabilityResolver resolver = new DataAvailabilityResolver(periods);

  @Test
  public void allMonthsPresent_isAvailable() {
    periods.add("hikaru", "CHESS_COM", false, "2026-05", FETCHED);
    periods.add("hikaru", "CHESS_COM", false, "2026-06", FETCHED);
    periods.add("hikaru", "CHESS_COM", false, "2026-07", FETCHED);

    DataAvailability data = resolve(completed("hikaru", "2026-05", "2026-07"));

    assertThat(data.status()).isEqualTo("AVAILABLE");
    assertThat(data.monthsAvailable()).isEqualTo(3);
    assertThat(data.monthsTotal()).isEqualTo(3);
  }

  @Test
  public void someMonthsSwept_isPartial() {
    periods.add("hikaru", "CHESS_COM", false, "2026-07", FETCHED);

    DataAvailability data = resolve(completed("hikaru", "2026-05", "2026-07"));

    assertThat(data.status()).isEqualTo("PARTIAL");
    assertThat(data.monthsAvailable()).isEqualTo(1);
    assertThat(data.monthsTotal()).isEqualTo(3);
  }

  @Test
  public void everyMonthSwept_isExpiredWithNoDeadlineLeft() {
    DataAvailability data = resolve(completed("hikaru", "2026-05", "2026-07"));

    assertThat(data.status()).isEqualTo("EXPIRED");
    assertThat(data.monthsAvailable()).isZero();
    assertThat(data.monthsTotal()).isEqualTo(3);
    assertThat(data.expiresAt()).isNull();
  }

  @Test
  public void expiresAtTracksTheEarliestFetch() {
    Instant oldest = Instant.parse("2026-07-18T09:00:00Z");
    periods.add("hikaru", "CHESS_COM", false, "2026-06", Instant.parse("2026-07-22T09:00:00Z"));
    periods.add("hikaru", "CHESS_COM", false, "2026-07", oldest);

    DataAvailability data = resolve(completed("hikaru", "2026-06", "2026-07"));

    // The request stops being whole when its first month drops out, not its last.
    assertThat(data.expiresAt()).isEqualTo(oldest.plus(RetentionPolicy.PERIOD));
  }

  @Test
  public void periodsForAnotherPlayerPlatformOrBulletSettingDoNotCount() {
    IndexingRequest request = completed("hikaru", "2026-07", "2026-07");
    periods.add("magnus", "CHESS_COM", false, "2026-07", FETCHED);
    periods.add("hikaru", "LICHESS", false, "2026-07", FETCHED);
    // indexed_periods is unique on exclude_bullet too: a bullet-excluding index is a different
    // corpus, so it cannot vouch for a request that kept bullet games.
    periods.add("hikaru", "CHESS_COM", true, "2026-07", FETCHED);

    assertThat(resolve(request).status()).isEqualTo("EXPIRED");

    periods.add("hikaru", "CHESS_COM", false, "2026-07", FETCHED);
    assertThat(resolve(request).status()).isEqualTo("AVAILABLE");
  }

  /**
   * The resolver keys on the request's own platform and excludeBullet, not on constants. Without
   * this, mutating {@code request.excludeBullet()} to {@code false} or {@code request.platform()}
   * to {@code "CHESS_COM"} leaves the rest of the suite green — every other request here is
   * CHESS_COM with bullet included.
   */
  @Test
  public void resolverReadsPlatformAndBulletSettingOffTheRequest() {
    IndexingRequest bulletExcluded =
        new IndexingRequest(
            UUID.randomUUID(),
            "hikaru",
            "LICHESS",
            "2026-07",
            "2026-07",
            "COMPLETED",
            FETCHED,
            FETCHED,
            null,
            42,
            true,
            false,
            0);

    // The period a CHESS_COM bullet-including request would have written cannot vouch for it.
    periods.add("hikaru", "CHESS_COM", false, "2026-07", FETCHED);
    assertThat(resolve(bulletExcluded).status()).isEqualTo("EXPIRED");

    periods.add("hikaru", "LICHESS", true, "2026-07", FETCHED);
    assertThat(resolve(bulletExcluded).status()).isEqualTo("AVAILABLE");
  }

  /**
   * A period is stored incomplete while its month is still running — which is every request that
   * covers the current month, the most common shape there is. The games are on disk either way, so
   * the answer to "is this data still here?" is yes; is_complete only governs whether the indexer
   * refetches.
   */
  @Test
  public void anIncompletePeriodStillCountsAsAvailable() {
    periods.add("hikaru", "CHESS_COM", false, "2026-07", FETCHED, false);

    DataAvailability data = resolve(completed("hikaru", "2026-07", "2026-07"));

    assertThat(data.status()).isEqualTo("AVAILABLE");
    assertThat(data.monthsAvailable()).isEqualTo(1);
  }

  @Test
  public void singleMonthRangeCountsOneMonth() {
    periods.add("hikaru", "CHESS_COM", false, "2026-07", FETCHED);

    DataAvailability data = resolve(completed("hikaru", "2026-07", "2026-07"));

    assertThat(data.monthsTotal()).isEqualTo(1);
    assertThat(data.status()).isEqualTo("AVAILABLE");
  }

  @Test
  public void rangeSpanningAYearBoundaryCountsEveryMonth() {
    DataAvailability data = resolve(completed("hikaru", "2025-11", "2026-02"));

    assertThat(data.monthsTotal()).isEqualTo(4);
  }

  @Test
  public void unfinishedRequestsAreNotReported() {
    periods.add("hikaru", "CHESS_COM", false, "2026-07", FETCHED);

    for (String status : List.of("PENDING", "PROCESSING", "FAILED")) {
      IndexingRequest request = request("hikaru", "2026-07", "2026-07", status);
      // Absent, not EXPIRED: nothing has been swept, the work simply hasn't produced data.
      assertThat(resolver.resolve(request)).as(status).isEmpty();
      assertThat(resolver.resolveAll(List.of(request))).as(status).isEmpty();
    }
  }

  @Test
  public void malformedStoredRangeIsReportedAsUnknown() {
    assertThat(resolve(completed("hikaru", "not-a-month", "2026-07")).status())
        .isEqualTo("UNKNOWN");
    assertThat(resolve(completed("hikaru", "2026-07", "2026-05")).status()).isEqualTo("UNKNOWN");
  }

  @Test
  public void aPageOfRequestsCostsOneStorageLookup() {
    List<IndexingRequest> page = new ArrayList<>();
    for (int i = 0; i < 20; i++) {
      page.add(completed("player" + i, "2026-07", "2026-07"));
    }
    page.add(request("pending", "2026-07", "2026-07", "PENDING"));

    assertThat(resolver.resolveAll(page)).hasSize(20);
    assertThat(periods.lookupCount).isEqualTo(1);
    // Only the players that could have data are asked about.
    assertThat(periods.lastPlayersQueried).hasSize(20).doesNotContain("pending");
  }

  @Test
  public void noCompletedRequestsSkipsStorageEntirely() {
    assertThat(resolver.resolveAll(List.of(request("x", "2026-07", "2026-07", "PENDING"))))
        .isEmpty();
    assertThat(resolver.resolveAll(List.of())).isEmpty();
    assertThat(periods.lookupCount).isZero();
  }

  private DataAvailability resolve(IndexingRequest request) {
    return resolver.resolve(request).orElseThrow();
  }

  private static IndexingRequest completed(String player, String startMonth, String endMonth) {
    return request(player, startMonth, endMonth, "COMPLETED");
  }

  private static IndexingRequest request(
      String player, String startMonth, String endMonth, String status) {
    return new IndexingRequest(
        UUID.randomUUID(),
        player,
        "CHESS_COM",
        startMonth,
        endMonth,
        status,
        FETCHED,
        FETCHED,
        null,
        42,
        false,
        false,
        0);
  }

  private static final class FakePeriodStore implements IndexedPeriodStore {
    private final List<IndexedPeriod> stored = new ArrayList<>();
    private int lookupCount;
    private Collection<String> lastPlayersQueried = List.of();

    void add(
        String player, String platform, boolean excludeBullet, String month, Instant fetchedAt) {
      add(player, platform, excludeBullet, month, fetchedAt, true);
    }

    void add(
        String player,
        String platform,
        boolean excludeBullet,
        String month,
        Instant fetchedAt,
        boolean isComplete) {
      stored.add(
          new IndexedPeriod(player, platform, month, fetchedAt, isComplete, 1, excludeBullet));
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
      return Optional.empty();
    }

    @Override
    public void upsertPeriod(
        String player,
        String platform,
        String month,
        Instant fetchedAt,
        boolean isComplete,
        int gamesCount,
        boolean excludeBullet) {}

    @Override
    public int deleteOlderThan(Instant threshold) {
      return 0;
    }
  }
}
