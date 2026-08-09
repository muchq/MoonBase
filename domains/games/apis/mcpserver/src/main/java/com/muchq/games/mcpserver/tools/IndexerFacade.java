package com.muchq.games.mcpserver.tools;

import com.muchq.games.one_d4.api.dto.AggregateRequest;
import com.muchq.games.one_d4.api.dto.AggregateResponse;
import com.muchq.games.one_d4.api.dto.AggregateRow;
import com.muchq.games.one_d4.api.dto.AnalyzeRequest;
import com.muchq.games.one_d4.api.dto.AnalyzeResponse;
import com.muchq.games.one_d4.api.dto.GameFeatureRow;
import com.muchq.games.one_d4.api.dto.IndexRequest;
import com.muchq.games.one_d4.api.dto.IndexResponse;
import com.muchq.games.one_d4.api.dto.QueryRequest;
import java.time.Duration;
import java.util.List;
import java.util.Optional;
import java.util.Set;
import java.util.UUID;

/**
 * What the MCP tools talk to. Every corpus-backed tool goes through here, and everything here goes
 * to one_d4 over HTTP.
 *
 * <p>This used to wrap in-process indexer components, which meant the MCP server ran a second
 * indexer against its own database — in the deployment, an in-memory one that started empty and was
 * thrown away on restart, so what a client indexed here never appeared on the site. The corpus
 * belongs to one_d4, and so does the code that manages it (#1332).
 *
 * <p>The tool-facing surface is unchanged on purpose: same methods, same shapes, and upstream 4xx
 * arrives as {@link IllegalArgumentException} — which is what the tools already catch — so a
 * rejected ChessQL query reads to an MCP client exactly as it did when the compiler ran in-process.
 */
public class IndexerFacade {

  /** COMPLETED or FAILED: nothing further will change, so polling can stop. */
  private static final Set<String> TERMINAL = Set.of("COMPLETED", "FAILED");

  static final Duration DEFAULT_INLINE_TIMEOUT = Duration.ofSeconds(60);
  static final Duration DEFAULT_POLL_INTERVAL = Duration.ofMillis(500);

  private final OneD4Client client;
  private final Duration inlineTimeout;
  private final Duration pollInterval;
  private final Sleeper sleeper;

  public IndexerFacade(OneD4Client client) {
    this(client, DEFAULT_INLINE_TIMEOUT, DEFAULT_POLL_INTERVAL, Thread::sleep);
  }

  IndexerFacade(
      OneD4Client client, Duration inlineTimeout, Duration pollInterval, Sleeper sleeper) {
    this.client = client;
    this.inlineTimeout = inlineTimeout;
    this.pollInterval = pollInterval;
    this.sleeper = sleeper;
  }

  /** Injectable so the polling tests do not spend real seconds proving they waited. */
  interface Sleeper {
    void sleep(long millis) throws InterruptedException;
  }

  /**
   * Submits an indexing request and, for a single month, waits for it.
   *
   * <p>The old in-process path called {@code IndexRequestService.submitHybrid}, which ran a single
   * month inline and returned a final status in one round trip. {@code POST /v1/index} is the
   * asynchronous entry point — {@code IndexController} calls {@code submit} — so the wait has to
   * happen here, as bounded polling. Without it the tool would answer PENDING for a request that
   * finishes a second later, and an assistant would have to learn to poll for the common case.
   *
   * <p>Multi-month requests are not waited on at all: they are the case the async API exists for,
   * and the tool has always returned PENDING for them.
   */
  public IndexResponse index(
      String player,
      String platform,
      String startMonth,
      String endMonth,
      boolean excludeBullet,
      boolean skipCache) {
    IndexResponse submitted;
    try {
      submitted =
          client.index(
              new IndexRequest(player, platform, startMonth, endMonth, excludeBullet, skipCache));
    } catch (IllegalArgumentException e) {
      throw new IllegalArgumentException(toToolFieldNames(e.getMessage()), e);
    }

    boolean singleMonth = startMonth != null && startMonth.equals(endMonth);
    if (!singleMonth || isTerminal(submitted)) {
      return submitted;
    }
    return awaitCompletion(submitted);
  }

  /**
   * Polls until the request reaches a terminal status or the budget runs out. A timeout is not an
   * error: the request is still running upstream and its id is in hand, so the caller gets the last
   * status seen and can follow it with {@code index_status}. Failing here would throw away a
   * perfectly good request id over a slow month.
   */
  private IndexResponse awaitCompletion(IndexResponse submitted) {
    long deadline = System.nanoTime() + inlineTimeout.toNanos();
    IndexResponse latest = submitted;
    while (System.nanoTime() < deadline) {
      try {
        sleeper.sleep(pollInterval.toMillis());
      } catch (InterruptedException e) {
        Thread.currentThread().interrupt();
        return latest;
      }
      Optional<IndexResponse> polled = client.status(submitted.id());
      if (polled.isEmpty()) {
        // The id one_d4 just handed out is gone. Nothing better to report than what it said.
        return latest;
      }
      latest = polled.get();
      if (isTerminal(latest)) {
        return latest;
      }
    }
    return latest;
  }

  private static boolean isTerminal(IndexResponse response) {
    return response.status() != null && TERMINAL.contains(response.status());
  }

  /**
   * one_d4 reports validation errors with its REST field names (player, startMonth, endMonth), but
   * the index_chess_games tool's arguments are username/start_month/end_month. Translate, so an MCP
   * client is pointed at arguments that exist on the tool it called.
   */
  private static String toToolFieldNames(String message) {
    if (message == null) {
      return null;
    }
    return message
        .replaceAll("\\bplayer\\b", "username")
        .replaceAll("\\bstartMonth\\b", "start_month")
        .replaceAll("\\bendMonth\\b", "end_month");
  }

  public Optional<IndexResponse> status(UUID requestId) {
    return client.status(requestId);
  }

  /** Runs a ChessQL query over indexed games; perspective fields resolve against {@code player}. */
  public List<GameFeatureRow> query(String chessql, String player, int limit) {
    // Coalesced, not trusted: one_d4's mapper omits null fields, so an empty result arrives with
    // no "games" key at all and deserializes to null. Handing that to a tool is an NPE inside the
    // response builder rather than the empty answer the caller asked for.
    return orEmpty(client.query(new QueryRequest(chessql, limit, 0, player)).games());
  }

  private static <T> List<T> orEmpty(List<T> maybeNull) {
    return maybeNull == null ? List.of() : maybeNull;
  }

  /**
   * Counts indexed games matching a ChessQL filter, grouped by the given fields. The untruncated
   * totals come back alongside the (limit-capped) groups so callers can tell when a long tail was
   * cut off — one_d4 computes them, including the second COUNT-over-groups scan it only pays for
   * when the limit was actually reached.
   */
  public AggregateResult aggregate(String chessql, List<String> groupBy, String player, int limit) {
    AggregateResponse response =
        client.aggregate(new AggregateRequest(chessql, groupBy, "count", limit, player));
    return new AggregateResult(
        orEmpty(response.groups()), response.totalGames(), response.totalGroups());
  }

  /** Aggregate result: the (possibly limit-truncated) groups plus the untruncated totals. */
  public record AggregateResult(List<AggregateRow> groups, long totalGames, long totalGroups) {}

  /** Detects motifs in a single PGN without indexing it. */
  public AnalyzeResponse analyze(String pgn) {
    if (pgn == null || pgn.isBlank()) {
      throw new IllegalArgumentException("pgn is required");
    }
    return client.analyze(new AnalyzeRequest(pgn));
  }
}
