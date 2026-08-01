package com.muchq.games.one_d4.db;

import java.time.Duration;
import java.time.Instant;
import java.util.List;
import java.util.Optional;
import java.util.UUID;

public interface IndexingRequestStore {

  /**
   * How many times a request may be claimed before it is presumed to be killing its workers.
   *
   * <p>Counted on claim rather than on failure, which is the conservative direction: a worker the
   * request kills before it can report anything still moves the counter. Three tolerates a
   * transient fault — a restart, a rolling deploy, one bad node — and is few enough that a genuine
   * poison pill does not tour the fleet.
   *
   * <p>On the interface rather than the DAO because it is part of the contract, not an
   * implementation detail: {@link #findExistingRequest} and {@link #reclaimStale} are both defined
   * in terms of it, so a fake that picks its own number models a different system than the one it
   * stands in for.
   */
  int MAX_ATTEMPTS = 3;

  /**
   * Atomically claims the live slot for this (player, platform, startMonth, endMonth,
   * excludeBullet), returning either a freshly created request or the one that already holds it.
   *
   * <p>This exists instead of a bare {@code create} because dedupe cannot be done as a read
   * followed by a write. Two identical submits — two REST threads, or REST and MCP, which are two
   * JVMs against one Postgres whenever {@code INDEXER_DB_URL} is set — can both find nothing and
   * both insert. {@code game_features} survives that on its {@code game_url} upsert, but {@code
   * motif_occurrences} does not: its flush deletes and re-inserts under fresh UUIDs in separate
   * transactions, so interleaved runs leave duplicated occurrence rows and every motif count for
   * those games reads double.
   *
   * <p>The caller must dispatch work only when {@link Claim#created()} is true. Adopting means
   * another caller already owns the work.
   *
   * <p>Implementations settle any abandoned holder first, as part of the same atomic step. Usually
   * that means releasing it back to the queue and letting this caller adopt it rather than start a
   * rival; only a request whose attempts are spent is retired, which frees the key so an insert can
   * succeed. See {@link #reclaimStale}.
   */
  Claim createOrAdopt(
      String player,
      String platform,
      String startMonth,
      String endMonth,
      boolean excludeBullet,
      boolean skipCache,
      Duration staleAfter,
      Instant now);

  /**
   * Takes the oldest live request nobody currently holds, and returns it — the table used as the
   * work queue.
   *
   * <p>This is what lets any worker run any request. Before it, a request could only ever be
   * processed by the process that accepted the submit, because the message went into that JVM's
   * {@code InMemoryIndexQueue}: adding an instance added no throughput for work already queued, a
   * restart lost the messages while the rows survived, and the two-JVM deployment sent load
   * wherever the submit happened to arrive.
   *
   * <p>Selection then {@link #claim}, rather than one statement, and deliberately so. {@code SELECT
   * ... FOR UPDATE SKIP LOCKED} would be the obvious shape and H2 does not have it; the conditional
   * UPDATE {@code claim} already performs is what makes this safe without it, since at most one
   * racer's WHERE can match. Losing the race costs a retry against the next candidate, not
   * correctness.
   *
   * <p>Ordered oldest-first because the queue it replaces was FIFO. Rows whose attempts are
   * exhausted are skipped — see {@link #reclaimStale}.
   */
  Optional<IndexingRequest> claimNext(String ownerId, Duration lease, Instant now);

  Optional<IndexingRequest> findById(UUID id);

  /**
   * Returns the live request with the same (player, platform, startMonth, endMonth, excludeBullet),
   * if there is one. Used to avoid creating duplicate indexing work.
   *
   * <p>Live means "still holds this range", which since #1279 is a narrower question than it looks.
   * A request whose worker died is still live: the work goes back in the queue, so a second submit
   * should adopt it rather than start a rival for the same games. A request sitting unclaimed is
   * live for the same reason. What ends a claim on a range is exhausting the attempts — after that
   * no worker may take it again, so it cannot go on holding the range against a resubmit.
   *
   * <p>Takes no clock, because it no longer ages rows out itself. That duplicated {@link
   * #reclaimStale}'s judgement, and once the stalled arm grew a fleet-liveness probe the two
   * answers diverged — this one would disown a queued row that reclamation would rightly leave
   * alone. The question is now purely "does a row still hold this range", and the clock belongs to
   * the one operation that settles rows.
   *
   * <p>Which leaves this with no production caller, and it should not acquire one — the same rule
   * {@link #holdsLease} is under, for the same reason. Reading this and then acting on the answer
   * is a check-then-act: two submits can both see nothing and both insert, which is the race {@link
   * #createOrAdopt} exists to close by folding the read into the claim. And since the read cannot
   * judge staleness any more, a caller that short-circuits on it skips the settle that {@code
   * createOrAdopt} does first, so a row nobody will ever run goes on answering resubmits forever.
   * Kept for tests and diagnostics, where "does anything hold this range" is a question worth
   * asking on its own.
   */
  Optional<IndexingRequest> findExistingRequest(
      String player, String platform, String startMonth, String endMonth, boolean excludeBullet);

  List<IndexingRequest> listRecent(int limit);

  void updateStatus(UUID id, String status, String errorMessage, int gamesIndexed);

  /**
   * Settles every request nobody is working on, and returns how many it touched. Three outcomes,
   * because "nobody is working on it" has three different meanings once the table is the queue.
   *
   * <ul>
   *   <li><b>Released.</b> A lease expired with attempts to spare. The owner is gone; the work is
   *       not. The row is unclaimed and left live for the next worker, keeping its {@code
   *       dedupe_key} because the range is still spoken for. This is the common case and it is
   *       silent — telling the user anything here would be a lie about work that is about to run.
   *   <li><b>Poisoned.</b> Claimed {@code MAX_ATTEMPTS} times, each worker stopping before it
   *       finished. Releasing is unbounded by construction, so without this a request that kills
   *       the process handling it tours the fleet forever, costing a worker each lap — a
   *       possibility that did not exist while a crashed process took its queue down with it.
   *   <li><b>Stalled.</b> Nobody holds it, nothing has touched it in {@code staleAfter}, and no
   *       worker <em>anywhere</em> holds a live lease. Every instance is down or partitioned, so
   *       nothing is going to run this and the user is owed an answer. Silence is the worse one.
   * </ul>
   *
   * <p>That last clause is doing real work, and leaving it out would recreate the bug two previous
   * changes were spent removing. Age alone cannot distinguish "nothing is serving this" from "my
   * turn has not come": one worker draining a deep backlog leaves rows at the back untouched for as
   * long as the backlog takes, and retiring those tells users to resubmit work that was about to
   * start. A live lease held by anyone is proof the fleet is working.
   *
   * <p>Implementations must apply the arms <b>poisoned, stalled, released</b> — which is not the
   * order they are listed in above, because that list is ordered by how often each happens rather
   * than by precedence. Two overlaps force it. A row whose attempts are spent is usually also
   * unheld and old, so it matches the stalled arm as well; both retire it, but only one gives the
   * user the real reason. And releasing stamps {@code updated_at}, which makes a row look freshly
   * touched and hides it from the stalled arm for another whole window.
   */
  int reclaimStale(Duration staleAfter, Instant now);

  /**
   * Takes ownership of a request for {@code lease}, if it is live and either unclaimed or held by a
   * lease that has already expired. Returns true if this caller now owns it.
   *
   * <p>Claiming is how a worker earns the right to write. Before this, liveness was inferred from
   * whether {@code updated_at} had moved recently, which cannot distinguish a worker that is slow
   * from one that is dead, and gives nothing to check a later write against.
   *
   * <p>{@code ownerId} identifies the <em>process</em>, not the thread or the run. It is the
   * fencing token every subsequent write is conditioned on, so it has to stay stable while a worker
   * holds the request and be unique across the instances competing for it.
   */
  boolean claim(UUID id, String ownerId, Duration lease, Instant now);

  /**
   * Extends this owner's lease. Returns false if the request is no longer live, or if {@code
   * owner_id} no longer names this caller — someone else has taken it, and the caller should stop.
   *
   * <p>Deliberately lenient about expiry, and that is not an oversight: a lease that has lapsed but
   * still names this owner has not been taken by anyone, so renewing it is recovery rather than a
   * second claim. A paused GC or a stalled connection pool can outlast a lease without anything
   * being wrong, and the alternative — abandoning a run nobody else wants — throws away work and
   * leaves the row stranded until the sweep.
   *
   * <p>What it will not do is re-take. Once {@code owner_id} has changed this returns false, and it
   * cannot race a takeover: both are single conditional UPDATEs on one row, so the row lock orders
   * them and the rival always wins. Refusing there is the point — the replacement is already
   * indexing that range, and reasserting would put two writers on the same games.
   */
  boolean renewLease(UUID id, String ownerId, Duration lease, Instant now);

  /**
   * Gives a claimed request back to the queue, unspent. Returns false if {@code owner_id} no longer
   * names this caller — someone else has it, and unclaiming it would strand them mid-run.
   *
   * <p>For a worker that is going away on purpose. {@link #reclaimStale}'s release arm covers the
   * owner that stopped answering, but it can only act once the lease has lapsed, because a lapsed
   * lease is the only evidence it has. A process being shut down knows sooner and can say so, which
   * turns five minutes of a stranded range into none.
   *
   * <p>It also returns the attempt, and that is the part that matters. {@code attempts} is spent on
   * claim so that a request which kills its worker outright still moves the counter — necessary,
   * and the reason the counter cannot tell a crash from a deploy on its own. Every process exit
   * would otherwise look like the request killed it, and a long-running request outliving three
   * rolling restarts would be retired as poisoned, telling the user its range fails repeatedly
   * about a fleet that is working perfectly. A hand-back is the missing evidence: the worker
   * survived long enough to say it was leaving, which is exactly what a killer request prevents.
   *
   * <p>Lenient about expiry, like {@link #renewLease} and for the same reason — a lease that lapsed
   * without anyone taking it is still this caller's to give back. Keeps {@code lease_expires_at},
   * because a fleet mid-deploy is the worst possible moment to look dead to {@link #reclaimStale}.
   */
  boolean handBack(UUID id, String ownerId, Instant now);

  /**
   * True if {@code ownerId} still holds a live, unexpired lease on this request.
   *
   * <p>Has no production caller, and should not acquire one: asking this before a write would be a
   * check-then-act with a window in it, which is exactly what {@link #updateStatusOwned} and {@link
   * GameFeatureStore#flushOwned} exist to avoid by folding the question into the write. It is here
   * for tests and for diagnostics.
   */
  boolean holdsLease(UUID id, String ownerId, Instant now);

  /**
   * Writes a status, but only if {@code ownerId} still holds the lease. Returns false without
   * writing if it does not — the caller has lost the request and should stop.
   *
   * <p>The fenced counterpart to {@link #updateStatus}. A worker holds a token, so every write it
   * makes is conditioned on that token, and this is the request row's half of the same rule {@link
   * GameFeatureStore#flushOwned} applies to the data plane. Without it a worker whose lease lapsed
   * could still stamp COMPLETED — and a terminal write clears {@code dedupe_key} and the lease, so
   * it would free the slot out from under whoever legitimately owns the range now, and report the
   * dead run's game count as the answer.
   *
   * <p>Non-owner callers keep using {@link #updateStatus}: the inline-dispatch failure path in
   * {@code IndexRequestService} writes on behalf of a request no worker ever claimed, and has no
   * token to present.
   */
  boolean updateStatusOwned(
      UUID id, String ownerId, String status, String errorMessage, int gamesIndexed, Instant now);

  /**
   * Deletes terminal request rows created before {@code threshold} that no {@code game_features}
   * row still references. Returns the number deleted.
   *
   * <p>Two guards, both narrowing what this can destroy rather than relying on the caller.
   *
   * <p>The reference check is belt and braces on top of sweep ordering: games are deleted first and
   * on a shorter clock, so by the time a request is old enough to sweep its games are already gone.
   * Checking anyway means the delete cannot raise a foreign key violation and abort the whole
   * retention pass even if the two clocks are ever reconfigured into overlap.
   *
   * <p>The status check stops a PENDING or PROCESSING request from being deleted out from under a
   * running worker. Today that is unreachable — {@link RetentionPolicy#REQUEST} is thirty days
   * against a one-hour staleness cutoff, and the worker reclaims stale rows earlier in the same
   * pass — but it is unreachable because of how the caller is sequenced, which is exactly the kind
   * of guarantee that evaporates when someone reorders the caller.
   */
  int deleteOlderThan(Instant threshold);

  /** The outcome of {@link #createOrAdopt}: the winning row, and whether this caller created it. */
  record Claim(IndexingRequest request, boolean created) {}

  record IndexingRequest(
      UUID id,
      String player,
      String platform,
      String startMonth,
      String endMonth,
      String status,
      Instant createdAt,
      Instant updatedAt,
      String errorMessage,
      int gamesIndexed,
      boolean excludeBullet,
      boolean skipCache,
      int attempts) {}
}
