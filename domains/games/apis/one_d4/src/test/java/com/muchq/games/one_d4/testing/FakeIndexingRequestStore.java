package com.muchq.games.one_d4.testing;

import com.muchq.games.one_d4.db.IndexingRequestStore;
import java.time.Duration;
import java.time.Instant;
import java.util.ArrayList;
import java.util.Comparator;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import java.util.Optional;
import java.util.UUID;

/**
 * The one {@link IndexingRequestStore} double (#1534): an in-memory table that models the rules the
 * submit path depends on rather than answering constants.
 *
 * <p>Worth modelling rather than stubbing, because the outcomes differ in exactly the way that
 * matters — a retired row frees its range for the next submit, a released one does not — and a stub
 * that returned {@code true} from {@link #claim} would let a service that resurrects released work
 * look correct.
 */
public final class FakeIndexingRequestStore implements IndexingRequestStore {
  private final Map<UUID, IndexingRequest> rows = new HashMap<>();
  private final Map<UUID, String> owners = new HashMap<>();
  private final Map<UUID, Instant> leases = new HashMap<>();
  private final Instant clock;
  private final List<IndexingRequest> created = new ArrayList<>();

  /** Stamps {@code updated_at} from the wall clock, for a suite whose service uses one too. */
  public FakeIndexingRequestStore() {
    this(Instant.now());
  }

  /**
   * Stamps {@code updated_at} from {@code clock}. {@link #updateStatus} is the unfenced write and
   * takes no instant, so the DAO stamps from its injected clock and this has to be given the same
   * one — a suite that pins its service to a fixed clock and leaves this on another is comparing
   * two clocks in every staleness check.
   */
  public FakeIndexingRequestStore(Instant clock) {
    this.clock = clock;
  }

  /**
   * Puts a row in the table without going through {@link #createOrAdopt}, for a test that needs a
   * request to already exist. Everything else — liveness, attempts, the range it holds — is then
   * decided by the same rules as a row this store minted itself.
   */
  public void seed(IndexingRequest request) {
    rows.put(request.id(), request);
  }

  /** The rows {@link #createOrAdopt} minted, in order. The row is the queue (#1279). */
  public List<IndexingRequest> created() {
    return List.copyOf(created);
  }

  public int createCallCount() {
    return created.size();
  }

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
    IndexingRequest row =
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
    rows.put(id, row);
    created.add(row);
    return new Claim(row, true);
  }

  /**
   * Unimplemented on purpose. The submit path never claims — it creates a row, and the C++ worker's
   * poller is the only thing that takes rows off the table. Returning empty here would be the
   * quieter choice and the worse one: if a later change makes the service claim, an empty answer
   * looks like "no work" and the test still passes.
   */
  @Override
  public Optional<IndexingRequest> claimNext(String ownerId, Duration lease, Instant now) {
    throw new UnsupportedOperationException(
        "the submit path does not claim; the worker's poller does");
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

  /**
   * Newest first and capped, as the DAO's ORDER BY created_at DESC LIMIT ? — the cap is the whole
   * contract of the list read, so a fake that returned everything let a wrong limit at the caller
   * go unnoticed.
   */
  @Override
  public List<IndexingRequest> listRecent(int limit) {
    return rows.values().stream()
        .sorted(Comparator.comparing(IndexingRequest::createdAt).reversed())
        .limit(limit)
        .toList();
  }

  @Override
  public void updateStatus(UUID id, String status, String errorMessage, int gamesIndexed) {
    IndexingRequest row = require(id);
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
  public boolean handBack(UUID id, String ownerId, Instant now) {
    IndexingRequest r = rows.get(id);
    if (r == null || !live(r) || !ownerId.equals(owners.get(id))) {
      return false;
    }
    owners.remove(id);
    // Returns the attempt: the worker survived long enough to say it was leaving, which is exactly
    // what a request that kills its worker prevents.
    rows.put(id, withAttempts(touched(r, now), Math.max(0, r.attempts() - 1)));
    return true;
  }

  @Override
  public boolean releaseOwned(UUID id, String ownerId, Instant now) {
    IndexingRequest r = rows.get(id);
    if (r == null || !live(r) || !ownerId.equals(owners.get(id))) {
      return false;
    }
    // No attempts arithmetic, unlike handBack above — the attempt stays spent.
    owners.remove(id);
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
      UUID id, String ownerId, String status, String errorMessage, int gamesIndexed, Instant now) {
    if (!holdsLease(id, ownerId, now)) {
      return false;
    }
    updateStatus(id, status, errorMessage, gamesIndexed);
    return true;
  }

  /**
   * Three arms, as in the DAO, applied poisoned then stalled then released.
   *
   * <p>Worth modelling rather than stubbing, and worth getting the arms right, because this is the
   * fake the submit path runs against and the two outcomes differ in exactly the way that matters
   * here: a retired row frees its range for the next submit, a released one does not. An earlier
   * version of this fake retired on lease expiry — the behaviour #1279 deleted — which would have
   * let a service that resurrects released work look correct.
   */
  @Override
  public int reclaimStale(Duration staleAfter, Instant now) {
    Instant cutoff = now.minus(staleAfter);
    List<IndexingRequest> candidates =
        rows.values().stream().filter(FakeIndexingRequestStore::live).toList();
    int settled = 0;
    for (IndexingRequest r : candidates) {
      boolean unheld = !owners.containsKey(r.id()) || !expiresAfter(r.id(), now);
      if (r.attempts() >= MAX_ATTEMPTS && unheld) {
        retire(r, "Abandoned: attempts exhausted", now);
      } else if (unheld && r.updatedAt().isBefore(cutoff) && !leasedSince(r, cutoff)) {
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

  /**
   * Any other row a worker held recently — the fleet-liveness probe the stalled arm turns on. One
   * check rather than the DAO's two: a lease that is still live is necessarily a recent one, so
   * "anyone holding a live lease" is subsumed by this, and the one case it would not cover — the
   * judged row being its own holder — is already excluded by {@code unheld}.
   */
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

  /**
   * Terminal rows only, as the DAO — the status guard is what stops a delete landing under a
   * running worker.
   */
  @Override
  public int deleteOlderThan(Instant threshold) {
    List<UUID> doomed =
        rows.values().stream()
            .filter(r -> !live(r))
            .filter(r -> r.createdAt().isBefore(threshold))
            .map(IndexingRequest::id)
            .toList();
    doomed.forEach(rows::remove);
    return doomed.size();
  }

  /** Backdates {@code updated_at}, so a test can age a row past the staleness window. */
  public void strand(UUID id, Instant updatedAt) {
    rows.put(id, touched(require(id), updatedAt));
  }

  /** A write against an id this store never held is a bug in the test, not a row to invent. */
  private IndexingRequest require(UUID id) {
    IndexingRequest row = rows.get(id);
    if (row == null) {
      throw new AssertionError("no request seeded or created with id " + id);
    }
    return row;
  }
}
