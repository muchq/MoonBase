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
   * <p>An hour is far longer than any healthy request takes (a twelve-month range is minutes) while
   * still being short enough that a user who hits a strand is not locked out for a working day.
   */
  public static final Duration STALE_REQUEST = Duration.ofHours(1);

  static {
    if (REQUEST.compareTo(PERIOD) <= 0) {
      throw new IllegalStateException(
          "REQUEST retention ("
              + REQUEST
              + ") must exceed PERIOD ("
              + PERIOD
              + "): game_features.request_id is a foreign key onto indexing_requests(id), so"
              + " deleting requests first would be blocked by their own games.");
    }
  }

  private RetentionPolicy() {}
}
