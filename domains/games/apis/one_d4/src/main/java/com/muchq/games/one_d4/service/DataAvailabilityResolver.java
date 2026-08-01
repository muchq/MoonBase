package com.muchq.games.one_d4.service;

import com.muchq.games.one_d4.api.dto.DataAvailability;
import com.muchq.games.one_d4.db.IndexedPeriodStore;
import com.muchq.games.one_d4.db.IndexingRequestStore.IndexingRequest;
import com.muchq.games.one_d4.db.RetentionPolicy;
import java.time.Instant;
import java.time.YearMonth;
import java.util.Collection;
import java.util.HashMap;
import java.util.HashSet;
import java.util.List;
import java.util.Map;
import java.util.Optional;
import java.util.Set;
import java.util.UUID;
import org.jspecify.annotations.Nullable;

/**
 * Answers "is this request's data still on disk?" for rows in {@code indexing_requests}.
 *
 * <p>Request rows outlive the games and indexed periods they produced — 30 days against 7 — so a
 * COMPLETED request from two weeks ago still reads "COMPLETED, 325 games" while querying it returns
 * nothing. That gap is deliberate (see {@link com.muchq.games.one_d4.db.RetentionPolicy}), and it
 * is exactly the window this resolver exists to describe: past 30 days the request is deleted too
 * and there is nothing left to ask about. This resolver closes that gap by checking each month the
 * request covers against the surviving {@code indexed_periods} rows, which retention deletes on the
 * same clock as the games.
 */
public class DataAvailabilityResolver {

  private final IndexedPeriodStore periodStore;

  public DataAvailabilityResolver(IndexedPeriodStore periodStore) {
    this.periodStore = periodStore;
  }

  /** Availability for one request, or empty when the request has no data to speak of. */
  public Optional<DataAvailability> resolve(IndexingRequest request) {
    return Optional.ofNullable(resolveAll(List.of(request)).get(request.id()));
  }

  /**
   * Availability for a page of requests, in one query. Requests that have not COMPLETED are absent
   * from the result — a PENDING request has no data yet, which is a different thing from having had
   * it swept, and reporting "expired" for it would be a lie.
   */
  public Map<UUID, DataAvailability> resolveAll(Collection<IndexingRequest> requests) {
    List<IndexingRequest> completed =
        requests.stream().filter(r -> "COMPLETED".equals(r.status())).toList();
    if (completed.isEmpty()) return Map.of();

    Set<String> players = new HashSet<>();
    for (IndexingRequest request : completed) {
      players.add(request.player());
    }

    Map<PeriodKey, Instant> fetchedAt = new HashMap<>();
    for (IndexedPeriodStore.IndexedPeriod period : periodStore.findPeriodsForPlayers(players)) {
      fetchedAt.put(
          new PeriodKey(period.player(), period.platform(), period.excludeBullet(), period.month()),
          period.fetchedAt());
    }

    Map<UUID, DataAvailability> result = new HashMap<>();
    for (IndexingRequest request : completed) {
      result.put(request.id(), availability(request, fetchedAt));
    }
    return result;
  }

  private static DataAvailability availability(
      IndexingRequest request, Map<PeriodKey, Instant> fetchedAt) {
    YearMonth start;
    YearMonth end;
    try {
      start = YearMonth.parse(request.startMonth());
      end = YearMonth.parse(request.endMonth());
    } catch (RuntimeException e) {
      // A malformed stored range can't be checked; say so rather than guessing at a status.
      return DataAvailability.unknown();
    }
    if (start.isAfter(end)) return DataAvailability.unknown();

    int total = 0;
    int available = 0;
    @Nullable Instant earliestFetch = null;
    for (YearMonth month = start; !month.isAfter(end); month = month.plusMonths(1)) {
      total++;
      Instant fetched =
          fetchedAt.get(
              new PeriodKey(
                  request.player(), request.platform(), request.excludeBullet(), month.toString()));
      if (fetched == null) continue;
      available++;
      // The request stops being whole when its earliest-fetched month is swept, so that is the
      // deadline worth reporting — not the last one to be refreshed.
      if (earliestFetch == null || fetched.isBefore(earliestFetch)) {
        earliestFetch = fetched;
      }
    }

    String status = available == 0 ? "EXPIRED" : available == total ? "AVAILABLE" : "PARTIAL";
    Instant expiresAt = earliestFetch == null ? null : earliestFetch.plus(RetentionPolicy.PERIOD);
    return new DataAvailability(status, available, total, expiresAt);
  }

  /** Exactly the columns {@code indexed_periods} is unique on. */
  private record PeriodKey(String player, String platform, boolean excludeBullet, String month) {}
}
