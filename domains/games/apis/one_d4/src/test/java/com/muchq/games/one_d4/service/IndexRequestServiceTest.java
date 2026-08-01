package com.muchq.games.one_d4.service;

import static org.assertj.core.api.Assertions.assertThat;
import static org.assertj.core.api.Assertions.assertThatThrownBy;

import com.muchq.games.one_d4.api.dto.IndexResponse;
import com.muchq.games.one_d4.db.IndexedPeriodStore;
import com.muchq.games.one_d4.db.IndexingRequestStore;
import com.muchq.games.one_d4.queue.IndexMessage;
import com.muchq.games.one_d4.queue.IndexQueue;
import java.time.Clock;
import java.time.Duration;
import java.time.Instant;
import java.time.ZoneOffset;
import java.util.ArrayList;
import java.util.Collection;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import java.util.Optional;
import java.util.UUID;
import org.junit.jupiter.api.BeforeEach;
import org.junit.jupiter.api.Test;

public class IndexRequestServiceTest {

  private static final Instant NOW = Instant.parse("2026-07-01T12:00:00Z");

  private InMemoryRequestStore requestStore;
  private RecordingQueue queue;
  private List<IndexMessage> inlineProcessed;
  private IndexRequestService service;

  @BeforeEach
  public void setUp() {
    requestStore = new InMemoryRequestStore();
    queue = new RecordingQueue();
    inlineProcessed = new ArrayList<>();
    service =
        new IndexRequestService(
            requestStore,
            queue,
            message -> {
              inlineProcessed.add(message);
              requestStore.updateStatus(message.requestId(), "COMPLETED", null, 42);
            },
            noPeriods(),
            Clock.fixed(NOW, ZoneOffset.UTC));
  }

  /** No period rows: availability resolves to EXPIRED, which these tests don't assert on. */
  private static DataAvailabilityResolver noPeriods() {
    return new DataAvailabilityResolver(
        new IndexedPeriodStore() {
          @Override
          public List<IndexedPeriod> findPeriodsForPlayers(Collection<String> players) {
            return List.of();
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
        });
  }

  private static IndexRequestService.Submission submission(String player, String platform) {
    return new IndexRequestService.Submission(player, platform, "2024-01", "2024-03", false, false);
  }

  @Test
  public void submit_createsEnqueuesAndReturnsPending() {
    IndexResponse response = service.submit(submission("hikaru", "CHESS_COM"));

    assertThat(response.status()).isEqualTo("PENDING");
    assertThat(response.player()).isEqualTo("hikaru");
    assertThat(queue.enqueued).hasSize(1);
    assertThat(inlineProcessed).isEmpty();
  }

  @Test
  public void submit_normalizesPlayerToLowercaseForDedupeAndCacheKeys() {
    IndexResponse first = service.submit(submission("Hikaru", "CHESS_COM"));
    IndexResponse second = service.submit(submission("HIKARU", "chess.com"));

    assertThat(first.player()).isEqualTo("hikaru");
    // Same lowercased identity → deduped onto the existing PENDING request
    assertThat(second.id()).isEqualTo(first.id());
    assertThat(queue.enqueued).hasSize(1);
  }

  @Test
  public void submit_acceptsChessComPlatformSpelling() {
    IndexResponse response = service.submit(submission("hikaru", "chess.com"));
    assertThat(response.platform()).isEqualTo("CHESS_COM");
    assertThat(queue.enqueued.get(0).platform()).isEqualTo("CHESS_COM");
  }

  @Test
  public void submit_skipCachePropagatesTheFlagWhenNothingIsInFlight() {
    IndexResponse forced =
        service.submit(
            new IndexRequestService.Submission(
                "hikaru", "CHESS_COM", "2024-01", "2024-03", false, true));

    assertThat(queue.enqueued).hasSize(1);
    assertThat(queue.enqueued.get(0).skipCache()).isTrue();
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

    assertThat(queue.enqueued).hasSize(1);
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

    assertThat(queue.enqueued).hasSize(2);
    assertThat(queue.enqueued.get(1).skipCache()).isTrue();
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
    assertThat(queue.enqueued).hasSize(1);
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
    assertThat(queue.enqueued).hasSize(2);
    assertThat(requestStore.findById(stranded.id()).orElseThrow().status()).isEqualTo("FAILED");
  }

  @Test
  public void submit_aFreshInFlightRequestIsStillReusedNotReplaced() {
    IndexResponse first = service.submit(submission("hikaru", "CHESS_COM"));
    requestStore.strand(first.id(), NOW.minus(Duration.ofMinutes(20)));

    IndexResponse second = service.submit(submission("hikaru", "CHESS_COM"));

    assertThat(second.id()).isEqualTo(first.id());
    assertThat(queue.enqueued).hasSize(1);
  }

  @Test
  public void submitHybrid_singleMonthRunsInlineAndReturnsFinalStatus() {
    IndexResponse response =
        service.submitHybrid(
            new IndexRequestService.Submission(
                "hikaru", "CHESS_COM", "2024-01", "2024-01", false, false));

    assertThat(inlineProcessed).hasSize(1);
    assertThat(queue.enqueued).isEmpty();
    assertThat(response.status()).isEqualTo("COMPLETED");
    assertThat(response.gamesIndexed()).isEqualTo(42);
  }

  @Test
  public void submitHybrid_multiMonthEnqueues() {
    IndexResponse response = service.submitHybrid(submission("hikaru", "CHESS_COM"));

    assertThat(inlineProcessed).isEmpty();
    assertThat(queue.enqueued).hasSize(1);
    assertThat(response.status()).isEqualTo("PENDING");
  }

  @Test
  public void submit_acceptsFullTwelveMonthRange() {
    IndexResponse response =
        service.submit(
            new IndexRequestService.Submission(
                "hikaru", "CHESS_COM", "2024-01", "2024-12", false, false));

    assertThat(response.status()).isEqualTo("PENDING");
    assertThat(queue.enqueued).hasSize(1);
  }

  @Test
  public void submitHybrid_dedupeHitReturnsExistingWithoutRunningInline() {
    IndexResponse first =
        service.submit(
            new IndexRequestService.Submission(
                "hikaru", "CHESS_COM", "2024-01", "2024-01", false, false));

    IndexResponse second =
        service.submitHybrid(
            new IndexRequestService.Submission(
                "hikaru", "CHESS_COM", "2024-01", "2024-01", false, false));

    assertThat(second.id()).isEqualTo(first.id());
    assertThat(inlineProcessed).isEmpty();
    assertThat(queue.enqueued).hasSize(1); // only the original submit's message
  }

  @Test
  public void submitHybrid_inlineFailurePropagatesFailedStatusAndError() {
    IndexRequestService failing =
        new IndexRequestService(
            requestStore,
            queue,
            message -> requestStore.updateStatus(message.requestId(), "FAILED", "chess.com 429", 0),
            noPeriods());

    IndexResponse response =
        failing.submitHybrid(
            new IndexRequestService.Submission(
                "hikaru", "CHESS_COM", "2024-01", "2024-01", false, false));

    assertThat(response.status()).isEqualTo("FAILED");
    assertThat(response.errorMessage()).isEqualTo("chess.com 429");
  }

  /**
   * The inline path owns its row outright — no queue entry, no worker to pick it up — so an
   * exception escaping the processor used to leave it PENDING forever, holding the range against
   * every later submit. The failure still reaches the caller; the row no longer outlives it.
   */
  @Test
  public void submitHybrid_inlineProcessorThrowingPropagatesAndRetiresTheRow() {
    IndexRequestService throwing =
        new IndexRequestService(
            requestStore,
            queue,
            message -> {
              throw new RuntimeException("boom");
            },
            noPeriods(),
            Clock.fixed(NOW, ZoneOffset.UTC));

    assertThatThrownBy(
            () ->
                throwing.submitHybrid(
                    new IndexRequestService.Submission(
                        "hikaru", "CHESS_COM", "2024-01", "2024-01", false, false)))
        .isInstanceOf(RuntimeException.class)
        .hasMessage("boom");

    assertThat(requestStore.findExistingRequest("hikaru", "CHESS_COM", "2024-01", "2024-01", false))
        .as("the range must not stay locked by a row nothing will ever finish")
        .isEmpty();

    IndexingRequestStore.IndexingRequest row =
        requestStore.listRecent(10).stream().findFirst().orElseThrow();
    assertThat(row.status()).isEqualTo("FAILED");
    assertThat(row.errorMessage()).contains("boom");
  }

  /** A retired inline failure frees the slot, so the caller can immediately try again. */
  @Test
  public void submitHybrid_rangeIsRequestableAgainAfterAnInlineFailure() {
    IndexRequestService throwing =
        new IndexRequestService(
            requestStore,
            queue,
            message -> {
              throw new RuntimeException("boom");
            },
            noPeriods(),
            Clock.fixed(NOW, ZoneOffset.UTC));

    assertThatThrownBy(
            () ->
                throwing.submitHybrid(
                    new IndexRequestService.Submission(
                        "hikaru", "CHESS_COM", "2024-01", "2024-01", false, false)))
        .isInstanceOf(RuntimeException.class);

    IndexResponse retry =
        service.submitHybrid(
            new IndexRequestService.Submission(
                "hikaru", "CHESS_COM", "2024-01", "2024-01", false, false));

    assertThat(retry.status()).isEqualTo("COMPLETED");
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
    assertThatThrownBy(() -> service.submit(submission("x", "lichess")))
        .isInstanceOf(IllegalArgumentException.class)
        .hasMessageContaining("Unsupported platform");
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

  /**
   * Models the constraint the real schema enforces: at most one live row per dedupe tuple, and a
   * terminal status releases it. A fake that lets two live rows coexist would let double-dispatch
   * tests pass here and fail against Postgres.
   */
  private static final class InMemoryRequestStore implements IndexingRequestStore {
    private final Map<UUID, IndexingRequest> rows = new HashMap<>();
    private final Map<UUID, String> owners = new HashMap<>();
    private final Map<UUID, Instant> leases = new HashMap<>();
    private Instant clock = Instant.parse("2026-07-01T12:00:00Z");

    private static boolean live(IndexingRequest r) {
      return r.status().equals("PENDING") || r.status().equals("PROCESSING");
    }

    private Optional<IndexingRequest> liveHolder(
        String player, String platform, String startMonth, String endMonth, boolean excludeBullet) {
      return rows.values().stream()
          .filter(
              r ->
                  r.player().equals(player)
                      && r.platform().equals(platform)
                      && r.startMonth().equals(startMonth)
                      && r.endMonth().equals(endMonth)
                      && r.excludeBullet() == excludeBullet
                      && live(r))
          .findFirst();
    }

    @Override
    public Claim createOrAdopt(
        String player,
        String platform,
        String startMonth,
        String endMonth,
        boolean excludeBullet,
        boolean skipCache,
        Duration staleAfter,
        Instant now) {
      reclaimStale(staleAfter, now);
      Optional<IndexingRequest> holder =
          liveHolder(player, platform, startMonth, endMonth, excludeBullet);
      if (holder.isPresent()) {
        return new Claim(holder.get(), false);
      }
      UUID id = UUID.randomUUID();
      IndexingRequest created =
          new IndexingRequest(
              id,
              player,
              platform,
              startMonth,
              endMonth,
              "PENDING",
              now,
              now,
              null,
              0,
              excludeBullet,
              skipCache,
              0);
      rows.put(id, created);
      return new Claim(created, true);
    }

    /**
     * Unimplemented on purpose. The submit path never claims — it creates a row and hands the work
     * to a worker, and {@code IndexWorkerLifecycle} is the only thing that takes rows off the
     * table. Returning empty here would be the quieter choice and the worse one: if a later change
     * makes the service claim, an empty answer looks like "no work" and the test still passes.
     */
    @Override
    public Optional<IndexingRequest> claimNext(String ownerId, Duration lease, Instant now) {
      throw new UnsupportedOperationException(
          "the submit path does not claim; see IndexWorkerLifecycleTest");
    }

    @Override
    public Optional<IndexingRequest> findById(UUID id) {
      return Optional.ofNullable(rows.get(id));
    }

    @Override
    public Optional<IndexingRequest> findExistingRequest(
        String player, String platform, String startMonth, String endMonth, boolean excludeBullet) {
      // Mirrors the DAO: live, and not yet out of attempts. Age and ownership deliberately do not
      // appear — a row whose worker died is still queued, so it still holds the range.
      return liveHolder(player, platform, startMonth, endMonth, excludeBullet)
          .filter(r -> r.attempts() < MAX_ATTEMPTS);
    }

    @Override
    public List<IndexingRequest> listRecent(int limit) {
      return List.copyOf(rows.values());
    }

    @Override
    public void updateStatus(UUID id, String status, String errorMessage, int gamesIndexed) {
      IndexingRequest row = rows.get(id);
      if (!"PENDING".equals(status) && !"PROCESSING".equals(status)) {
        owners.remove(id);
        leases.remove(id);
      }
      rows.put(
          id,
          new IndexingRequest(
              row.id(),
              row.player(),
              row.platform(),
              row.startMonth(),
              row.endMonth(),
              status,
              row.createdAt(),
              clock,
              errorMessage,
              gamesIndexed,
              row.excludeBullet(),
              row.skipCache(),
              row.attempts()));
    }

    @Override
    public boolean claim(UUID id, String ownerId, Duration lease, Instant now) {
      IndexingRequest r = rows.get(id);
      if (r == null || !live(r)) {
        return false;
      }
      String held = owners.get(id);
      Instant expires = leases.get(id);
      boolean heldByAnother =
          held != null && !held.equals(ownerId) && expires != null && expires.isAfter(now);
      if (heldByAnother) {
        return false;
      }
      boolean reclaiming = !ownerId.equals(held);
      owners.put(id, ownerId);
      leases.put(id, now.plus(lease));
      // Counted per new owner, as in the DAO — re-claiming your own row is a renewal, not a lap.
      rows.put(id, withAttempts(touched(r, now), reclaiming ? r.attempts() + 1 : r.attempts()));
      return true;
    }

    private static IndexingRequest withAttempts(IndexingRequest r, int attempts) {
      return new IndexingRequest(
          r.id(),
          r.player(),
          r.platform(),
          r.startMonth(),
          r.endMonth(),
          r.status(),
          r.createdAt(),
          r.updatedAt(),
          r.errorMessage(),
          r.gamesIndexed(),
          r.excludeBullet(),
          r.skipCache(),
          attempts);
    }

    @Override
    public boolean renewLease(UUID id, String ownerId, Duration lease, Instant now) {
      IndexingRequest r = rows.get(id);
      // Deliberately lenient about expiry, matching the DAO: renewing a lapsed-but-unstolen lease
      // is the recovery path. Only a change of owner_id ends a worker's claim.
      if (r == null || !live(r) || !ownerId.equals(owners.get(id))) {
        return false;
      }
      leases.put(id, now.plus(lease));
      rows.put(id, touched(r, now));
      return true;
    }

    @Override
    public boolean holdsLease(UUID id, String ownerId, Instant now) {
      IndexingRequest r = rows.get(id);
      Instant expires = leases.get(id);
      return r != null
          && live(r)
          && ownerId.equals(owners.get(id))
          && expires != null
          && expires.isAfter(now);
    }

    @Override
    public boolean updateStatusOwned(
        UUID id,
        String ownerId,
        String status,
        String errorMessage,
        int gamesIndexed,
        Instant now) {
      if (!holdsLease(id, ownerId, now)) {
        return false;
      }
      updateStatus(id, status, errorMessage, gamesIndexed);
      return true;
    }

    /**
     * Three arms, as in the DAO, applied poisoned then stalled then released.
     *
     * <p>Worth modelling rather than stubbing, and worth getting the arms right, because this is
     * the fake the submit path runs against and the two outcomes differ in exactly the way that
     * matters here: a retired row frees its range for the next submit, a released one does not. An
     * earlier version of this fake retired on lease expiry — the behaviour #1279 deleted — which
     * would have let a service that resurrects released work look correct.
     */
    @Override
    public int reclaimStale(Duration staleAfter, Instant now) {
      Instant cutoff = now.minus(staleAfter);
      List<IndexingRequest> candidates =
          rows.values().stream().filter(InMemoryRequestStore::live).toList();
      boolean anyoneWorking =
          rows.values().stream()
              .anyMatch(r -> owners.containsKey(r.id()) && expiresAfter(r.id(), now));

      int settled = 0;
      for (IndexingRequest r : candidates) {
        boolean unheld = !owners.containsKey(r.id()) || !expiresAfter(r.id(), now);
        if (r.attempts() >= MAX_ATTEMPTS && unheld) {
          retire(r, "Abandoned: attempts exhausted", now);
        } else if (unheld
            && r.updatedAt().isBefore(cutoff)
            && !anyoneWorking
            && !leasedSince(r, cutoff)) {
          retire(r, "Abandoned: no worker running anywhere", now);
        } else if (owners.containsKey(r.id()) && !expiresAfter(r.id(), now)) {
          // Released: the owner is gone, the work is not. The row stays live and keeps its range.
          owners.remove(r.id());
          rows.put(r.id(), touched(r, now));
        } else {
          continue;
        }
        settled++;
      }
      return settled;
    }

    private boolean expiresAfter(UUID id, Instant now) {
      Instant expires = leases.get(id);
      return expires != null && expires.isAfter(now);
    }

    /** Any other row a worker held recently — the DAO's second liveness probe. */
    private boolean leasedSince(IndexingRequest judged, Instant cutoff) {
      return rows.values().stream()
          .anyMatch(
              other ->
                  !other.id().equals(judged.id())
                      && leases.get(other.id()) != null
                      && !leases.get(other.id()).isBefore(cutoff));
    }

    private void retire(IndexingRequest r, String reason, Instant now) {
      owners.remove(r.id());
      leases.remove(r.id());
      rows.put(
          r.id(),
          new IndexingRequest(
              r.id(),
              r.player(),
              r.platform(),
              r.startMonth(),
              r.endMonth(),
              "FAILED",
              r.createdAt(),
              now,
              reason,
              r.gamesIndexed(),
              r.excludeBullet(),
              r.skipCache(),
              r.attempts()));
    }

    private static IndexingRequest touched(IndexingRequest r, Instant now) {
      return new IndexingRequest(
          r.id(),
          r.player(),
          r.platform(),
          r.startMonth(),
          r.endMonth(),
          r.status(),
          r.createdAt(),
          now,
          r.errorMessage(),
          r.gamesIndexed(),
          r.excludeBullet(),
          r.skipCache(),
          r.attempts());
    }

    @Override
    public int deleteOlderThan(Instant threshold) {
      List<UUID> doomed =
          rows.values().stream()
              .filter(r -> r.createdAt().isBefore(threshold))
              .map(IndexingRequest::id)
              .toList();
      doomed.forEach(rows::remove);
      return doomed.size();
    }

    /** Backdates a row's updatedAt so a test can strand it without waiting an hour. */
    /** Backdates {@code updated_at}, so a test can age a row past the staleness window. */
    void strand(UUID id, Instant updatedAt) {
      rows.put(id, touched(rows.get(id), updatedAt));
    }
  }

  private static final class RecordingQueue implements IndexQueue {
    private final List<IndexMessage> enqueued = new ArrayList<>();

    @Override
    public void enqueue(IndexMessage message) {
      enqueued.add(message);
    }

    @Override
    public Optional<IndexMessage> poll(Duration timeout) {
      return Optional.empty();
    }

    @Override
    public int size() {
      return enqueued.size();
    }
  }
}
