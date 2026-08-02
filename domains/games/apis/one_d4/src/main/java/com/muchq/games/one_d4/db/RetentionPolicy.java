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
   * for hours, renewing throughout, up to {@link #MAX_RUN} — and a lease that lapses without anyone
   * taking it is renewed rather than abandoned, so a stalled pool costs a warning and not a run.
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

  /**
   * How long one claim may go on being renewed before the worker has to let go (#1282).
   *
   * <p>The third clock, and it answers the question the other two structurally cannot. {@link
   * #LEASE} asks "is the owner still there?", which the owner answers itself by renewing, and
   * {@link #STALE_REQUEST} asks "is anyone serving this at all?". A worker wedged mid-run — blocked
   * forever on a hung socket, a deadlocked pool, an unbounded retry — answers yes to both,
   * honestly: it is there, and it is serving this. Its heartbeat runs on a separate thread and
   * keeps renewing whatever the run is doing, so no liveness probe can catch it. What is unknown is
   * not whether the worker is alive but whether the <em>run</em> is going anywhere, and only
   * elapsed time can speak to that.
   *
   * <p>Past this the heartbeat stops and the run's thread is interrupted. The lease then lapses
   * within {@link #LEASE} and the ordinary release path applies, so the request goes back to the
   * queue rather than being failed outright — costing one of {@code MAX_ATTEMPTS} rather than an
   * answer.
   *
   * <p>The two halves recover different things and neither implies the other. Letting the lease
   * lapse recovers the <em>request</em>: another worker takes the row and gets through it. The
   * interrupt recovers the <em>worker</em>, whose poller is a single thread — without it the
   * instance holding a wedged run stops taking work until someone restarts it, so a fleet would
   * recover its rows one at a time while shedding a worker for each one. It is best-effort by
   * nature: it ends the run only if whatever the run is blocked on honours an interrupt, which the
   * JDK {@code HttpClient} sends and body reads this worker actually waits in do (#1282) — a body
   * read leaves the interrupt status set itself, while a send throws with it cleared and {@code
   * Jdk11HttpClient} restores it on the way out — and a lock held forever by another thread does
   * not. The row is recovered either way.
   *
   * <p>Neither half bounds writes. A run that crosses the ceiling with a valid lease may finish
   * what it is already inside; what it may not do is start the next month or renew its way back in.
   *
   * <p>Six hours is deliberately far above any legitimate run. A twelve-month range for a prolific
   * player is the worst case — one archive fetch and a profile lookup per distinct opponent per
   * month, plus replaying every game — and that is minutes, not hours, against a healthy chess.com.
   * The cost of being wrong is asymmetric: too high only delays recovering from a wedge, while too
   * low interrupts real work and spends an attempt doing it, three of which retire the request with
   * a message blaming the range. So it is set where crossing it is evidence of a fault rather than
   * of a big request, and crossing it is logged as one.
   *
   * <p>Must exceed {@link #STALE_REQUEST} — though not for the reason it first looks like. The
   * replacement does not inherit an aged row: the heartbeat stamps {@code updated_at} on every
   * beat, and {@link IndexingRequestStore#releaseOwned} stamps it again on the way out. It is that
   * a ceiling below the staleness window would cut runs short of the very window the system uses to
   * decide whether anything is happening at all, which is incoherent — a run still going is the
   * clearest evidence there is that something is. Asserted in {@code IndexE2ETest} rather than
   * here, where a violation would surface as an {@code ExceptionInInitializerError} during
   * Micronaut startup.
   */
  public static final Duration MAX_RUN = Duration.ofHours(6);

  private RetentionPolicy() {}
}
