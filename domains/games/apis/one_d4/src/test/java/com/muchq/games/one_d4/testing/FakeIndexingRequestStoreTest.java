package com.muchq.games.one_d4.testing;

import static org.assertj.core.api.Assertions.assertThat;
import static org.assertj.core.api.Assertions.assertThatThrownBy;

import com.muchq.games.one_d4.db.IndexingRequestStore;
import com.muchq.games.one_d4.db.IndexingRequestStore.Claim;
import com.muchq.games.one_d4.db.IndexingRequestStore.IndexingRequest;
import java.time.Duration;
import java.time.Instant;
import java.util.UUID;
import org.junit.jupiter.api.Test;

/** The seeding and recording this store grew when it became the shared one (#1534). */
class FakeIndexingRequestStoreTest {

  private static final Instant NOW = Instant.parse("2026-07-01T12:00:00Z");
  private static final Duration STALE_AFTER = Duration.ofHours(1);
  private static final Duration LEASE = Duration.ofMinutes(5);

  private final FakeIndexingRequestStore store = new FakeIndexingRequestStore();

  @Test
  void aSeededRowIsFoundById() {
    IndexingRequest seeded = pending("hikaru");
    store.seed(seeded);

    assertThat(store.findById(seeded.id())).contains(seeded);
  }

  // Seeding puts the row in the table rather than programming an answer, so the store's own
  // rules decide the rest — a seeded live row holds its range against a duplicate submit.
  @Test
  void aSeededLiveRowIsAdoptedRatherThanDuplicated() {
    store.seed(pending("hikaru"));

    Claim claim =
        store.createOrAdopt(
            "hikaru", "CHESS_COM", "2024-01", "2024-01", false, false, STALE_AFTER, NOW);

    assertThat(claim.created()).isFalse();
    assertThat(store.createCallCount()).isZero();
    assertThat(store.created()).isEmpty();
  }

  @Test
  void createOrAdoptRecordsTheRowsItMinted() {
    store.createOrAdopt(
        "hikaru", "CHESS_COM", "2024-01", "2024-01", false, false, STALE_AFTER, NOW);
    store.createOrAdopt(
        "magnus", "CHESS_COM", "2024-01", "2024-01", false, false, STALE_AFTER, NOW);

    assertThat(store.createCallCount()).isEqualTo(2);
    assertThat(store.created())
        .extracting(IndexingRequest::player)
        .containsExactly("hikaru", "magnus");
  }

  /** A terminal row holds no range, so the next submit creates rather than adopts. */
  @Test
  void aSeededTerminalRowDoesNotHoldItsRange() {
    store.seed(withStatus(pending("hikaru"), "COMPLETED"));

    Claim claim =
        store.createOrAdopt(
            "hikaru", "CHESS_COM", "2024-01", "2024-01", false, false, STALE_AFTER, NOW);

    assertThat(claim.created()).isTrue();
  }

  // IndexController asks for 50 and that cap is the whole contract of the list read. A fake that
  // ignored the limit let a wrong constant there go unnoticed.
  @Test
  void listRecentHonoursTheLimit() {
    store.seed(pendingAt("a", NOW));
    store.seed(pendingAt("b", NOW.minusSeconds(1)));
    store.seed(pendingAt("c", NOW.minusSeconds(2)));

    assertThat(store.listRecent(2)).hasSize(2);
    assertThat(store.listRecent(0)).isEmpty();
  }

  /** Newest first, like the DAO's ORDER BY created_at DESC — so the limit drops the oldest. */
  @Test
  void listRecentReturnsNewestFirst() {
    store.seed(pendingAt("oldest", NOW.minusSeconds(2)));
    store.seed(pendingAt("newest", NOW));
    store.seed(pendingAt("middle", NOW.minusSeconds(1)));

    assertThat(store.listRecent(10))
        .extracting(IndexingRequest::player)
        .containsExactly("newest", "middle", "oldest");
    assertThat(store.listRecent(2))
        .extracting(IndexingRequest::player)
        .containsExactly("newest", "middle");
  }

  // updateStatus takes no instant, so the stamp comes from the clock the store was built with.
  // Left implicit, a suite pinned to a fixed clock silently compares it against wall time.
  @Test
  void updateStatusStampsFromTheClockItWasGiven() {
    Instant clock = Instant.parse("2030-01-01T00:00:00Z");
    FakeIndexingRequestStore fixed = new FakeIndexingRequestStore(clock);
    IndexingRequest seeded = pending("hikaru");
    fixed.seed(seeded);

    fixed.updateStatus(seeded.id(), "COMPLETED", "", 7);

    assertThat(fixed.findById(seeded.id()))
        .get()
        .extracting(IndexingRequest::updatedAt)
        .isEqualTo(clock);
  }

  // The pair that decides whether a wedged request ever retires. handBack is a worker leaving on
  // purpose, so it returns the attempt; releaseOwned is a run that was cut loose at MAX_RUN, so the
  // attempt stays spent. Collapse the two and a request that wedges every time loops forever on a
  // counter that never moves, and the poison arm never fires.
  @Test
  void handBackReturnsTheAttemptAndReleaseOwnedKeepsItSpent() {
    IndexingRequest handed = claimed("handed");
    IndexingRequest released = claimed("released");

    assertThat(store.handBack(handed.id(), "worker", NOW)).isTrue();
    assertThat(store.releaseOwned(released.id(), "worker", NOW)).isTrue();

    assertThat(attemptsOf(handed.id())).isZero();
    assertThat(attemptsOf(released.id())).isEqualTo(1);
  }

  /** Neither hands back a request someone else now owns. */
  @Test
  void neitherReleasesARequestAnotherWorkerHolds() {
    IndexingRequest taken = claimed("taken");

    assertThat(store.handBack(taken.id(), "someone-else", NOW)).isFalse();
    assertThat(store.releaseOwned(taken.id(), "someone-else", NOW)).isFalse();
    assertThat(attemptsOf(taken.id())).isEqualTo(1);
  }

  // A worker renewing across its own retries must not spend the budget doing it. Counted per new
  // owner, as in the DAO.
  @Test
  void reClaimingYourOwnRowIsARenewalNotALap() {
    IndexingRequest row = claimed("hikaru");

    assertThat(store.claim(row.id(), "worker", LEASE, NOW.plusSeconds(1))).isTrue();
    assertThat(attemptsOf(row.id())).isEqualTo(1);

    assertThat(store.claim(row.id(), "other-worker", LEASE, NOW.plus(Duration.ofHours(1))))
        .isTrue();
    assertThat(attemptsOf(row.id())).isEqualTo(2);
  }

  // The poisoned arm. Releasing is unbounded by construction, so without this a request that kills
  // the process handling it tours the fleet forever.
  @Test
  void aRequestWhoseAttemptsAreSpentIsRetiredAsPoisoned() {
    IndexingRequest spent = pending("poison");
    store.seed(withAttempts(spent, IndexingRequestStore.MAX_ATTEMPTS));

    assertThat(store.reclaimStale(STALE_AFTER, NOW)).isEqualTo(1);

    IndexingRequest after = store.findById(spent.id()).orElseThrow();
    assertThat(after.status()).isEqualTo("FAILED");
    assertThat(after.errorMessage()).contains("attempts exhausted");
  }

  // The stalled arm's fleet-liveness clause. Age alone cannot tell "nothing is serving this" from
  // "my turn has not come": one worker draining a backlog leaves rows at the back untouched.
  @Test
  void aLiveLeaseAnywhereProtectsAnOldUnheldRow() {
    IndexingRequest queued = pending("at-the-back");
    store.seed(queued);
    store.strand(queued.id(), NOW.minus(Duration.ofHours(6)));
    claimed("being-worked");

    assertThat(store.reclaimStale(STALE_AFTER, NOW)).isZero();
    assertThat(store.findById(queued.id()).orElseThrow().status()).isEqualTo("PENDING");
  }

  /** With nobody holding anything and nothing leased recently, the same row is retired. */
  @Test
  void anOldUnheldRowIsRetiredWhenNoWorkerIsRunningAnywhere() {
    IndexingRequest queued = pending("at-the-back");
    store.seed(queued);
    store.strand(queued.id(), NOW.minus(Duration.ofHours(6)));

    assertThat(store.reclaimStale(STALE_AFTER, NOW)).isEqualTo(1);
    IndexingRequest after = store.findById(queued.id()).orElseThrow();
    assertThat(after.status()).isEqualTo("FAILED");
    assertThat(after.errorMessage()).contains("no worker running anywhere");
  }

  // The second liveness probe: a lease another row held recently is still evidence of a fleet,
  // even once it has lapsed.
  @Test
  void aRecentlyHeldLeaseAnywhereProtectsAnOldUnheldRow() {
    IndexingRequest queued = pending("at-the-back");
    store.seed(queued);
    store.strand(queued.id(), NOW.minus(Duration.ofHours(6)));

    IndexingRequest recent = pending("just-finished");
    store.seed(recent);
    store.claim(recent.id(), "worker", Duration.ofMinutes(1), NOW.minusSeconds(120));

    assertThat(store.reclaimStale(STALE_AFTER, NOW)).isEqualTo(1);
    assertThat(store.findById(queued.id()).orElseThrow().status()).isEqualTo("PENDING");
  }

  // The released arm, and the distinction #1279 turns on: an expired lease means the owner is
  // gone, not that the work is. The row stays live and keeps its range.
  @Test
  void anExpiredLeaseReleasesTheRowRatherThanRetiringIt() {
    IndexingRequest row = claimed("hikaru");

    Instant afterLease = NOW.plus(LEASE).plusSeconds(1);
    assertThat(store.reclaimStale(STALE_AFTER, afterLease)).isEqualTo(1);

    IndexingRequest after = store.findById(row.id()).orElseThrow();
    assertThat(after.status()).isEqualTo("PENDING");
    assertThat(store.holdsLease(row.id(), "worker", afterLease)).isFalse();
    assertThat(
            store
                .createOrAdopt(
                    "hikaru",
                    "CHESS_COM",
                    "2024-01",
                    "2024-01",
                    false,
                    false,
                    STALE_AFTER,
                    afterLease)
                .created())
        .as("a released row still holds its range")
        .isFalse();
  }

  // Terminal rows only. Retention runs on a thirty-day clock against a one-hour staleness cutoff,
  // so an old live row means a worker is on it — deleting that out from under them is the thing
  // the status guard exists to stop.
  @Test
  void deleteOlderThanSparesALiveRowHoweverOldItIs() {
    IndexingRequest live = pendingAt("live", Instant.EPOCH);
    IndexingRequest done = withStatus(pendingAt("done", Instant.EPOCH), "COMPLETED");
    store.seed(live);
    store.seed(done);

    assertThat(store.deleteOlderThan(NOW)).isEqualTo(1);

    assertThat(store.findById(live.id())).isPresent();
    assertThat(store.findById(done.id())).isEmpty();
  }

  /** A row nobody may claim again cannot go on holding its range against a resubmit. */
  @Test
  void findExistingRequestIgnoresARequestWhoseAttemptsAreSpent() {
    IndexingRequest spent = pending("hikaru");
    store.seed(withAttempts(spent, IndexingRequestStore.MAX_ATTEMPTS));

    assertThat(store.findExistingRequest("hikaru", "CHESS_COM", "2024-01", "2024-01", false))
        .isEmpty();
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

  /** Seeds a live row and claims it once, so it carries exactly one spent attempt. */
  private IndexingRequest claimed(String player) {
    IndexingRequest row = pending(player);
    store.seed(row);
    assertThat(store.claim(row.id(), "worker", LEASE, NOW)).isTrue();
    assertThat(attemptsOf(row.id())).isEqualTo(1);
    return row;
  }

  private int attemptsOf(UUID id) {
    return store.findById(id).orElseThrow().attempts();
  }

  // Writing against an id nobody seeded is a bug in the test. Inventing the row would let it pass.
  @Test
  void aWriteAgainstAnUnknownIdFailsLoudly() {
    UUID unknown = UUID.randomUUID();

    assertThatThrownBy(() -> store.updateStatus(unknown, "COMPLETED", "", 0))
        .isInstanceOf(AssertionError.class)
        .hasMessageContaining(unknown.toString());
    assertThatThrownBy(() -> store.strand(unknown, NOW)).isInstanceOf(AssertionError.class);
  }

  private static IndexingRequest pending(String player) {
    return pendingAt(player, NOW);
  }

  private static IndexingRequest pendingAt(String player, Instant createdAt) {
    return new IndexingRequest(
        UUID.randomUUID(),
        player,
        "CHESS_COM",
        "2024-01",
        "2024-01",
        "PENDING",
        createdAt,
        createdAt,
        null,
        0,
        false,
        false,
        0);
  }

  private static IndexingRequest withStatus(IndexingRequest r, String status) {
    return new IndexingRequest(
        r.id(),
        r.player(),
        r.platform(),
        r.startMonth(),
        r.endMonth(),
        status,
        r.createdAt(),
        r.updatedAt(),
        r.errorMessage(),
        r.gamesIndexed(),
        r.excludeBullet(),
        r.skipCache(),
        r.attempts());
  }
}
