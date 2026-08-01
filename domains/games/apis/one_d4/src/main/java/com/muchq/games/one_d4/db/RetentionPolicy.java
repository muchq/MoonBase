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
   * A PENDING or PROCESSING request whose {@code updated_at} is older than this is presumed
   * abandoned by a dead owner and retired to FAILED.
   *
   * <p>Without a bound, a single stranded row blocks its (player, platform, month range) forever:
   * dedupe keeps returning it, and the unique constraint on {@code dedupe_key} keeps a replacement
   * from being created. Rows get stranded by ordinary events — a restart with work still in the
   * process-local queue, or an inline MCP dispatch that throws before the worker takes ownership —
   * so "rare" is not the same as "never".
   *
   * <p>A running request cannot age out however slow chess.com is, because {@code IndexWorker}
   * heartbeats it on a timer for as long as it is working — see {@code IndexWorker.startHeartbeat}.
   * Status writes alone were not enough: they land at month boundaries and every hundredth game, so
   * a single month under that size reported nothing between its first and last write, and the
   * archive fetch inside that span goes through an HTTP client with no request timeout.
   *
   * <p>What the hour still does <em>not</em> cover is time spent waiting in the queue. {@code
   * InMemoryIndexQueue} is drained by a single thread, and a queued message has no worker to
   * heartbeat for it, so its {@code updated_at} stays frozen at insert. A backlog deeper than an
   * hour therefore retires work that is owned and about to run, telling the user to re-submit while
   * the message is still queued. That is tracked separately rather than papered over here; the fix
   * is to beat on enqueue as well, not a bigger number, because raising the window trades one wrong
   * answer for a slower one.
   *
   * <p>The invariant that {@link #REQUEST} must exceed {@link #PERIOD} is asserted in {@code
   * IndexE2ETest} alongside the existing check on PERIOD, not in a static initializer here. These
   * are compile-time constants six lines apart, so a violation can only be introduced by editing
   * this file — and a static block would report it as an {@code ExceptionInInitializerError} during
   * Micronaut startup rather than as a failing test.
   */
  public static final Duration STALE_REQUEST = Duration.ofHours(1);

  private RetentionPolicy() {}
}
