package com.muchq.games.one_d4.db;

import java.time.Duration;

/**
 * How long indexed data survives before the retention worker deletes it.
 *
 * <p>This lives next to the stores rather than in the worker because two callers have to agree on
 * it: the worker that does the deleting, and the API that tells clients when a request's data is
 * due to disappear. A request row in {@code indexing_requests} outlives the games it produced, so
 * without a shared constant the reported expiry would silently drift from the actual sweep.
 */
public final class RetentionPolicy {

  /** Games and indexed periods are deleted once they are older than this. */
  public static final Duration PERIOD = Duration.ofDays(7);

  /**
   * Request rows are deleted once they are older than this — deliberately longer than {@link
   * #PERIOD}, for two independent reasons.
   *
   * <p>The first is a correctness constraint, not a preference: {@code game_features.request_id} is
   * a foreign key onto {@code indexing_requests(id)}, so a request may not be deleted while any
   * game still points at it. Outliving the games it produced is what makes the sweep safe.
   *
   * <p>The second is that a request row is the only thing left that can explain its own emptiness.
   * Once its months are swept, the API reports the request as {@code EXPIRED} rather than dropping
   * it, which answers "what happened to my index?" with "pruned — re-run it" instead of silence.
   * Deleting the row on the same clock as the data would throw that answer away at the exact moment
   * it becomes useful, so the gap between the two windows is the window in which the explanation
   * exists.
   */
  public static final Duration REQUEST = Duration.ofDays(30);

  /**
   * How long a live request that <em>nobody has claimed</em> may sit before it is presumed orphaned
   * and retired to FAILED.
   *
   * <p>Without a bound, a single stranded row blocks its (player, platform, month range) forever:
   * dedupe keeps returning it, and the unique constraint on {@code dedupe_key} keeps a replacement
   * from being created. Rows get stranded by ordinary events — a restart with work still in the
   * process-local queue, or an inline MCP dispatch that throws before the worker takes ownership —
   * so "rare" is not the same as "never".
   *
   * <p>This is measured in hours because it is the wrong question asked of an unanswerable
   * situation. There is no owner whose silence could be measured, so the only evidence is the age
   * of the row, and the row's age says nothing about whether the work is about to start. For a
   * request that <em>has</em> been claimed there is a better question and a much shorter clock: see
   * {@link #LEASE}.
   *
   * <p>What the hour still does not cover is time spent waiting in the queue, and the cost of that
   * went <em>up</em> when ownership arrived. {@code InMemoryIndexQueue} is drained by a single
   * thread, and a queued message has no worker holding a lease for it, so its {@code updated_at}
   * stays frozen at insert and a backlog deeper than an hour retires work that is owned and about
   * to run. The worker then reaches the message, fails to claim a retired row, and declines: the
   * indexing does not happen at all. Before ownership it carried on and its unfenced terminal write
   * landed, so the user got their games — at the cost of exactly the concurrent-writer corruption
   * this design prevents, since the freed dedupe slot may already have started a replacement.
   *
   * <p>Neither answer is right, and a bigger number only makes the wrong one slower. The fix is for
   * queued work not to be invisible: dispatch claiming from this table instead of from memory,
   * tracked as #1279.
   *
   * <p>The invariant that {@link #REQUEST} must exceed {@link #PERIOD} is asserted in {@code
   * IndexE2ETest} alongside the existing check on PERIOD, not in a static initializer here. These
   * are compile-time constants a few lines apart, so a violation can only be introduced by editing
   * this file — and a static block would report it as an {@code ExceptionInInitializerError} during
   * Micronaut startup rather than as a failing test.
   */
  public static final Duration STALE_REQUEST = Duration.ofHours(1);

  /**
   * How long a worker's claim on a request is good for without renewal.
   *
   * <p>Deliberately far shorter than {@link #STALE_REQUEST}, because it answers a different
   * question. The hour above asks "did anyone ever pick this up?" — a question about a request
   * nobody owns. This asks "is the owner still there?", which the owner itself answers by renewing
   * every {@link #LEASE_RENEWAL}, so it can be decided in minutes rather than an hour.
   *
   * <p>What "decided" means today is narrower than it sounds, and worth stating rather than
   * implying. A lapsed lease makes the request <em>reclaimable</em>; nothing picks it up. {@code
   * IndexWorker.process} is the only caller of {@code claim}, and it runs on a dispatched message,
   * so there is no scanner looking for expired leases and no re-dispatch. The row is retired to
   * FAILED and the range becomes requestable again — the user resubmits. Reclamation itself is only
   * as prompt as its caller: {@code RetentionWorker} is on an hourly schedule, and the only faster
   * path is {@code createOrAdopt}, which retires the key it is about to claim. So the five minutes
   * bounds when a lapse <em>may</em> be acted on, not when it is. Actually handing the range to
   * another worker is #1279.
   *
   * <p>Five minutes is four renewal intervals: three consecutive misses are survivable and expiry
   * lands on the fourth. Short enough that a crashed instance does not strand a range for longer
   * than a user will wait before resubmitting. It is not a timeout on the work — a request can run
   * for hours, renewing throughout — and a lease that lapses without anyone taking it is renewed
   * rather than abandoned, so a stalled pool costs a warning and not a run.
   *
   * <p>{@code IndexWorker.DEFAULT_HEARTBEAT_INTERVAL} must stay below this, and the relationship is
   * asserted in {@code IndexE2ETest} rather than left to inspection — an interval longer than the
   * lease means every lease lapses between beats, which does not fail loudly. It shows up as work
   * being reclaimed out from under healthy workers.
   */
  public static final Duration LEASE = Duration.ofMinutes(5);

  /**
   * How often the holder renews. A quarter of {@link #LEASE}, so three consecutive renewals can be
   * lost — to a paused GC, a saturated pool, a brief database blip — before anyone else may take
   * the request.
   */
  public static final Duration LEASE_RENEWAL = LEASE.dividedBy(4);

  private RetentionPolicy() {}
}
