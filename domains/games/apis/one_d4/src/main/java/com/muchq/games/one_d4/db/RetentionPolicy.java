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
   * How long a request may sit untouched, with no worker running anywhere, before it is retired and
   * the user is told.
   *
   * <p>This used to mean "nobody ever picked this up, so it is orphaned" — a reasonable reading
   * while dispatch came from a process-local queue, where a row nobody had claimed usually meant a
   * message that died with its process. Once workers claim from the table (#1279), an unclaimed row
   * is simply queued, and retiring it on age alone throws away work that was about to run and tells
   * the user to resubmit into the same queue.
   *
   * <p>So the window survives, but qualified: it fires only when no worker anywhere holds a live
   * lease. That is the difference between a backlog and an outage. A backlog is the system working;
   * an outage — every instance down, or partitioned from this database — is the one case where
   * nothing will ever pick the work up, and the user needs an answer rather than an indefinite
   * PENDING. See {@link IndexingRequestStore#reclaimStale}.
   *
   * <p>An hour is generous for detecting an outage and harmless for a healthy fleet, where a queued
   * request is claimed within seconds and its {@code updated_at} moves. The invariant that {@link
   * #REQUEST} must exceed {@link #PERIOD} is asserted in {@code IndexE2ETest} rather than in a
   * static initializer here, where a violation would surface as an {@code
   * ExceptionInInitializerError} during Micronaut startup instead of as a failing test.
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
   * <p>A lapsed lease returns the work to the queue rather than ending it: the sweep clears the
   * owner and any worker may claim it next (#1279). The five minutes still bounds when that
   * <em>may</em> happen rather than when it does, because reclamation is only as prompt as its
   * caller — {@code RetentionWorker} runs hourly, and the one faster path is {@code createOrAdopt}
   * settling the key it is about to claim.
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
