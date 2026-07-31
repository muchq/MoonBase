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

  private RetentionPolicy() {}
}
