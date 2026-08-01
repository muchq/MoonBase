package com.muchq.games.one_d4.worker;

import com.muchq.games.chess_com_client.ChessClient;
import com.muchq.games.chess_com_client.GamesResponse;
import com.muchq.games.chess_com_client.PlayedGame;
import com.muchq.games.chess_com_client.Player;
import com.muchq.games.chess_com_client.PlayerResult;
import com.muchq.games.one_d4.api.dto.GameFeature;
import com.muchq.games.one_d4.db.GameFeatureStore;
import com.muchq.games.one_d4.db.IndexedPeriodStore;
import com.muchq.games.one_d4.db.IndexingRequestStore;
import com.muchq.games.one_d4.db.RetentionPolicy;
import com.muchq.games.one_d4.engine.FeatureExtractor;
import com.muchq.games.one_d4.engine.model.GameFeatures;
import com.muchq.games.one_d4.engine.model.Motif;
import com.muchq.games.one_d4.queue.IndexMessage;
import dev.failsafe.Failsafe;
import dev.failsafe.RetryPolicy;
import java.sql.SQLException;
import java.time.Clock;
import java.time.Duration;
import java.time.Instant;
import java.time.YearMonth;
import java.time.ZoneOffset;
import java.time.format.DateTimeFormatter;
import java.util.ArrayList;
import java.util.Collections;
import java.util.HashMap;
import java.util.LinkedHashMap;
import java.util.LinkedHashSet;
import java.util.List;
import java.util.Map;
import java.util.Optional;
import java.util.Set;
import java.util.UUID;
import java.util.concurrent.CancellationException;
import java.util.concurrent.ExecutionException;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;
import java.util.concurrent.Future;
import java.util.concurrent.ScheduledExecutorService;
import java.util.concurrent.ScheduledFuture;
import java.util.concurrent.TimeUnit;
import java.util.regex.Matcher;
import java.util.regex.Pattern;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

public class IndexWorker {
  private static final Logger LOG = LoggerFactory.getLogger(IndexWorker.class);
  private static final DateTimeFormatter MONTH_FORMAT = DateTimeFormatter.ofPattern("yyyy-MM");
  private static final Pattern ECO_PATTERN = Pattern.compile("\\[ECO\\s+\"([^\"]+)\"\\]");
  static final int BATCH_SIZE = 100;

  private static final RetryPolicy<Void> UPSERT_RETRY =
      RetryPolicy.<Void>builder()
          .handleIf(IndexWorker::isLockConflict)
          .withBackoff(Duration.ofMillis(100), Duration.ofSeconds(1))
          .withMaxAttempts(3)
          .onRetry(
              e ->
                  LOG.warn(
                      "Lock conflict on upsertPeriod, retrying (attempt {})", e.getAttemptCount()))
          .build();

  private final ChessClient chessClient;
  private final FeatureExtractor featureExtractor;
  private final IndexingRequestStore requestStore;
  private final GameFeatureStore gameFeatureStore;
  private final IndexedPeriodStore periodStore;
  private final ExecutorService extractionExecutor;
  private final Clock clock;
  private final Duration heartbeatInterval;
  private final ScheduledExecutorService heartbeatScheduler;

  /**
   * Identifies this worker as the holder of a lease. Stable for the life of the process and unique
   * across the instances competing for the same table, which is what makes it usable as a fencing
   * token: every write this worker makes is conditioned on the request still naming it. The host
   * and pid are carried for the sake of whoever reads the column while debugging a stuck range.
   */
  private final String ownerId = buildOwnerId();

  private static String buildOwnerId() {
    String host;
    try {
      host = java.net.InetAddress.getLocalHost().getHostName();
    } catch (Exception e) {
      host = "unknown-host";
    }
    return host + "/" + ProcessHandle.current().pid() + "/" + UUID.randomUUID();
  }

  /** Visible so a test can assert which worker holds a request. */
  public String ownerId() {
    return ownerId;
  }

  /**
   * How often the lease is renewed. Paced against {@link RetentionPolicy#LEASE}, not against {@link
   * RetentionPolicy#STALE_REQUEST}: the hour is how long an <em>unclaimed</em> row may sit before
   * it is presumed orphaned, and beating on that schedule against a five-minute lease would let
   * every lease lapse between beats.
   */
  public static final Duration DEFAULT_HEARTBEAT_INTERVAL = RetentionPolicy.LEASE_RENEWAL;

  public IndexWorker(
      ChessClient chessClient,
      FeatureExtractor featureExtractor,
      IndexingRequestStore requestStore,
      GameFeatureStore gameFeatureStore,
      IndexedPeriodStore periodStore,
      ExecutorService extractionExecutor) {
    this(
        chessClient,
        featureExtractor,
        requestStore,
        gameFeatureStore,
        periodStore,
        extractionExecutor,
        Clock.systemUTC());
  }

  public IndexWorker(
      ChessClient chessClient,
      FeatureExtractor featureExtractor,
      IndexingRequestStore requestStore,
      GameFeatureStore gameFeatureStore,
      IndexedPeriodStore periodStore,
      ExecutorService extractionExecutor,
      Clock clock) {
    this(
        chessClient,
        featureExtractor,
        requestStore,
        gameFeatureStore,
        periodStore,
        extractionExecutor,
        clock,
        DEFAULT_HEARTBEAT_INTERVAL);
  }

  /**
   * @param clock the source for {@code indexed_periods.fetched_at}, which retention compares
   *     against. Injected so a test can advance time across the retention boundary instead of
   *     backdating rows, which only ever exercises {@code deleteOlderThan} and never the window
   *     arithmetic that decides what "old" means.
   * @param heartbeatInterval how often an in-flight request's lease is renewed. Injected only so a
   *     test can observe a beat without waiting out a renewal interval — the heartbeat is scheduled
   *     on wall-clock time, not on {@code clock}, because its job is to prove this process is still
   *     alive and a fake clock proves nothing about that.
   */
  public IndexWorker(
      ChessClient chessClient,
      FeatureExtractor featureExtractor,
      IndexingRequestStore requestStore,
      GameFeatureStore gameFeatureStore,
      IndexedPeriodStore periodStore,
      ExecutorService extractionExecutor,
      Clock clock,
      Duration heartbeatInterval) {
    this.chessClient = chessClient;
    this.featureExtractor = featureExtractor;
    this.requestStore = requestStore;
    this.gameFeatureStore = gameFeatureStore;
    this.periodStore = periodStore;
    this.extractionExecutor = extractionExecutor;
    this.clock = clock;
    this.heartbeatInterval = heartbeatInterval;
    this.heartbeatScheduler =
        Executors.newSingleThreadScheduledExecutor(
            r -> {
              Thread t = new Thread(r, "index-request-heartbeat");
              // Daemon: a heartbeat must never be the reason a JVM refuses to exit.
              t.setDaemon(true);
              return t;
            });
  }

  public void process(IndexMessage message) {
    LOG.info(
        "Processing index request {} for player={} platform={}",
        message.requestId(),
        message.player(),
        message.platform());

    if (!requestStore.claim(message.requestId(), ownerId, RetentionPolicy.LEASE, clock.instant())) {
      // Three causes, and the message must not pick one: a live rival holds the lease, the row
      // reached a terminal status, or it is gone. Only the first is benign — it means the work is
      // already owned and doing it again would put two writers on the same games.
      //
      // The second is not benign and is the known cost of dispatching from a process-local queue.
      // A message that waits longer than the unclaimed cutoff has its row retired underneath it,
      // and this worker then declines the work rather than doing it: the dedupe slot is free, so a
      // replacement may already be running the range. Before ownership existed the worker carried
      // on and its unfenced terminal write landed, so the user got their games — at the cost of
      // exactly the concurrent-writer corruption this change prevents. Neither answer is right;
      // the fix is for queued work not to be retired at all, which needs dispatch to claim from
      // the table rather than from memory (#1279).
      LOG.warn(
          "Declining request {}: it is not available to claim (already held, or retired while"
              + " queued). No games will be indexed for it.",
          message.requestId());
      return;
    }

    ScheduledFuture<?> heartbeat = startHeartbeat(message.requestId());
    try {
      progress(message.requestId(), 0);

      YearMonth start = YearMonth.parse(message.startMonth(), MONTH_FORMAT);
      YearMonth end = YearMonth.parse(message.endMonth(), MONTH_FORMAT);
      int totalIndexed = 0;

      // Player titles fetched once per distinct username per request (lowercased key). Confirmed
      // lookups — including not-found/untitled — are cached; API errors are not, so they retry on
      // a later month and mark the affected period incomplete for refetch.
      Map<String, String> titleCache = new HashMap<>();

      for (YearMonth month = start; !month.isAfter(end); month = month.plusMonths(1)) {
        String monthStr = month.format(MONTH_FORMAT);
        // Stamped before the month's games are written, not after. game_features.indexed_at comes
        // from the database as each batch lands, so a period stamped afterwards is strictly newer
        // than the games it vouches for — and retention, comparing both against one threshold,
        // would delete the games first and leave the period claiming they are still there. Taking
        // the timestamp up front inverts that: the period expires at or before its games, so the
        // reported status can be pessimistic but never a lie.
        Instant fetchedAt = clock.instant();
        if (!message.skipCache()) {
          Optional<IndexedPeriodStore.IndexedPeriod> cached =
              periodStore.findCompletePeriod(
                  message.player(), message.platform(), monthStr, message.excludeBullet());
          if (cached.isPresent()) {
            int count = cached.get().gamesCount();
            totalIndexed += count;
            LOG.debug(
                "Skipping fetch for player={} platform={} month={} (cached, games={})",
                message.player(),
                message.platform(),
                monthStr,
                count);
            progress(message.requestId(), totalIndexed);
            continue;
          }
        }

        Optional<GamesResponse> response = chessClient.fetchGames(message.player(), month);
        if (response.isEmpty()) {
          // chess.com 404s the archive for a month the player has no games in. The month *was*
          // indexed — the answer is "none" — so record an empty period rather than dropping
          // through. Skipping it would leave the month indistinguishable from one retention has
          // swept (DataAvailabilityResolver reads a missing row as "gone"), and would make the
          // period cache miss forever, refetching an empty archive on every request.
          LOG.warn("No games found for player={} month={}", message.player(), month);
          // Ownership first. upsertPeriod is the one write in a run that carries no token —
          // indexed_periods is keyed by (player, platform, month) and has no request to fence
          // against — so the only protection it can have is not reaching it after the lease is
          // gone. Writing an empty period for a month a replacement has already indexed properly
          // would make the period cache report zero games and skip that month until retention
          // sweeps it.
          progress(message.requestId(), totalIndexed);
          upsertPeriod(message, monthStr, month, fetchedAt, 0, false);
          continue;
        }

        List<GameFeature> featureBatch = new ArrayList<>();
        Map<String, Map<Motif, List<GameFeatures.MotifOccurrence>>> occurrencesBatch =
            new LinkedHashMap<>();

        List<PlayedGame> games = new ArrayList<>();
        for (PlayedGame game : response.get().games()) {
          if (message.excludeBullet() && "bullet".equals(game.timeClass())) {
            continue;
          }
          games.add(game);
        }

        // Resolve titles (deduped, bounded by the extraction pool size) before extraction work is
        // submitted, so the pool is otherwise idle and total chess.com concurrency stays capped.
        TitleResolution titleResolution = resolveTitles(games, titleCache);
        Map<String, String> titles = titleResolution.titles();

        // Submit each surviving game to the extraction pool, preserving source order.
        List<Future<ExtractResult>> futures = new ArrayList<>();
        for (PlayedGame game : games) {
          futures.add(
              extractionExecutor.submit(
                  () -> {
                    try {
                      GameFeatures features = featureExtractor.extract(game.pgn());
                      GameFeature row = buildGameFeature(message, game, features, titles);
                      return new ExtractResult(row, game.url(), features.occurrences());
                    } catch (Exception e) {
                      // TODO: pair futures with their game URLs in the drain loop instead of
                      // smuggling the URL through an exception message — the wrapper is only
                      // here so the warn log below can identify which game failed.
                      throw new RuntimeException(
                          "Failed to extract features for game " + game.url(), e);
                    }
                  }));
        }

        int monthCount = 0;
        for (Future<ExtractResult> future : futures) {
          ExtractResult result;
          try {
            result = future.get();
          } catch (ExecutionException e) {
            LOG.warn("{}", e.getCause().getMessage(), e.getCause());
            continue;
          } catch (InterruptedException e) {
            Thread.currentThread().interrupt();
            LOG.warn("Interrupted while draining extraction futures", e);
            break;
          }
          featureBatch.add(result.row());
          if (!result.occurrences().isEmpty()) {
            occurrencesBatch.put(result.gameUrl(), result.occurrences());
          }
          monthCount++;
          totalIndexed++;
          if (featureBatch.size() >= BATCH_SIZE) {
            flushBatch(message.requestId(), featureBatch, occurrencesBatch);
            progress(message.requestId(), totalIndexed);
          }
        }
        flushBatch(message.requestId(), featureBatch, occurrencesBatch);
        if (titleResolution.degraded()) {
          LOG.warn(
              "Title enrichment degraded for player={} month={}; period stored incomplete",
              message.player(),
              monthStr);
        }
        upsertPeriod(message, monthStr, month, fetchedAt, monthCount, titleResolution.degraded());
        progress(message.requestId(), totalIndexed);
      }

      final int finalCount = totalIndexed;
      if (fenced(
          message.requestId(),
          () ->
              requestStore.updateStatusOwned(
                  message.requestId(), ownerId, "COMPLETED", null, finalCount, clock.instant()),
          "the run could be completed")) {
        LOG.info("Completed indexing request {} with {} games", message.requestId(), totalIndexed);
      } else {
        // Logging the count unconditionally read as success whatever the row said. The games are
        // on disk either way; what is lost is the record that this run produced them.
        LOG.warn(
            "Indexed {} games for request {} but could not record COMPLETED: the lease is gone.",
            totalIndexed,
            message.requestId());
      }
    } catch (LeaseLostException e) {
      // No terminal write is attempted, and that is the whole difference from an ordinary failure.
      // It is not that a FAILED here would damage the replacement's row — that write is fenced
      // too, and would simply be refused. It is that attempting it would log a spurious error for
      // a run that did not fail: it was stopped, correctly, because the range moved.
      LOG.warn("Abandoning request {}: {}", message.requestId(), e.getMessage());
    } catch (Exception e) {
      LOG.error("Failed to process index request {}", message.requestId(), e);
      // Through fenced(), like every other write in the run. This is the write most likely to be
      // refused for a reason unrelated to what it is reporting — the database problem that throws
      // out of a run is the same one that stalls the heartbeat — and it is the only write that can
      // explain the failure. Refused, it leaves the row PROCESSING holding its dedupe slot, so the
      // range stays blocked until the hourly sweep retires it under a message about an owner that
      // stopped responding, which is not what happened.
      if (!fenced(
          message.requestId(),
          () ->
              requestStore.updateStatusOwned(
                  message.requestId(),
                  ownerId,
                  "FAILED",
                  "Indexing failed due to an internal error",
                  0,
                  clock.instant()),
          "the failure could be recorded")) {
        // Only reachable now when the range has genuinely moved on, in which case the replacement
        // owns the outcome and this run has nothing to report.
        LOG.error(
            "Could not record FAILED for request {}: the lease is gone. The row will be reclaimed"
                + " rather than reporting this error.",
            message.requestId());
      }
    } finally {
      heartbeat.cancel(false);
    }
  }

  /**
   * Renews this worker's lease for as long as the request is being worked on, so reclamation can
   * tell "running slowly" from "abandoned".
   *
   * <p>On a timer rather than at checkpoints, because the spans that need covering are exactly the
   * ones with no checkpoint in them: a single archive fetch, a run of profile lookups, the
   * extraction of a month holding fewer than {@link #BATCH_SIZE} games. The interval is a quarter
   * of the lease, so three consecutive beats can be lost — to a paused GC, a blocked pool, a
   * database blip — before anyone else may take the request.
   *
   * <p>Renewal asserts that this owner is still here; it says nothing about progress, which is why
   * a request can run for hours without looking abandoned. A beat that comes back false means the
   * row no longer names this worker as owner, so something has already handed the range to a
   * replacement. The heartbeat stops rather than reasserting itself — retaking it would put two
   * writers on the same games — and the run unwinds at its next fenced write.
   */
  private ScheduledFuture<?> startHeartbeat(UUID requestId) {
    long everyMillis = Math.max(1, heartbeatInterval.toMillis());
    return heartbeatScheduler.scheduleWithFixedDelay(
        () -> {
          try {
            if (!requestStore.renewLease(
                requestId, ownerId, RetentionPolicy.LEASE, clock.instant())) {
              LOG.warn(
                  "Request {} is no longer live; another owner has taken this range. Stopping"
                      + " heartbeat.",
                  requestId);
              throw new CancellationException("request no longer live");
            }
          } catch (CancellationException e) {
            throw e;
          } catch (RuntimeException e) {
            // A missed beat is survivable — the interval leaves room for three. Logging and
            // continuing beats killing the schedule on one transient database error.
            LOG.warn("Heartbeat failed for request {}", requestId, e);
          }
        },
        everyMillis,
        everyMillis,
        TimeUnit.MILLISECONDS);
  }

  /**
   * Records that this month was indexed, whatever it turned out to contain.
   *
   * <p>Every fetched month gets a row, including one whose archive 404'd — a missing row means "not
   * indexed", and both the period cache and {@code DataAvailabilityResolver} read it that way.
   *
   * <p>A period is complete only once the month itself is over, so the current month is always
   * refetched, and a month whose title lookups saw API errors is stored incomplete so the next
   * request refetches it (upserting titles via ON CONFLICT) instead of freezing nulls.
   */
  private void upsertPeriod(
      IndexMessage message,
      String monthStr,
      YearMonth month,
      Instant fetchedAt,
      int gamesCount,
      boolean degraded) {
    Instant firstDayNextMonth =
        month.plusMonths(1).atDay(1).atStartOfDay(ZoneOffset.UTC).toInstant();
    boolean isComplete = !fetchedAt.isBefore(firstDayNextMonth) && !degraded;
    Failsafe.with(UPSERT_RETRY)
        .run(
            () ->
                periodStore.upsertPeriod(
                    message.player(),
                    message.platform(),
                    monthStr,
                    fetchedAt,
                    isComplete,
                    gamesCount,
                    message.excludeBullet()));
  }

  private GameFeature buildGameFeature(
      IndexMessage message, PlayedGame game, GameFeatures features, Map<String, String> titles) {
    // PlayedGame.eco carries the chess.com ECOUrl (human-readable opening line); the ECO code
    // itself comes from the PGN [ECO "..."] tag.
    String openingName = Openings.nameFromEcoUrl(game.eco());
    return new GameFeature(
        null, // id generated by DB
        message.requestId(),
        game.url(),
        message.platform(),
        game.whiteResult() != null ? game.whiteResult().username() : null,
        game.blackResult() != null ? game.blackResult().username() : null,
        game.whiteResult() != null ? Integer.valueOf(game.whiteResult().rating()) : null,
        game.blackResult() != null ? Integer.valueOf(game.blackResult().rating()) : null,
        titleFor(titles, game.whiteResult()),
        titleFor(titles, game.blackResult()),
        game.timeClass(),
        extractEcoFromPgn(game.pgn()),
        openingName,
        Openings.familyFromName(openingName),
        determineResult(game),
        game.endTime(),
        features.numMoves(),
        clock.instant(),
        game.pgn());
  }

  /**
   * Ensures every distinct username in {@code games} has a title entry in {@code cache}, fetching
   * missing profiles concurrently on the extraction pool (which bounds fan-out against chess.com).
   * Successful lookups — including confirmed not-found/untitled players — are cached; API errors
   * are NOT cached (so a later month or request retries them) and mark the resolution degraded.
   * Returns an immutable snapshot safe to read from extraction threads.
   */
  private TitleResolution resolveTitles(List<PlayedGame> games, Map<String, String> cache) {
    Set<String> missing = new LinkedHashSet<>();
    for (PlayedGame game : games) {
      collectMissingUsername(game.whiteResult(), cache, missing);
      collectMissingUsername(game.blackResult(), cache, missing);
    }

    boolean degraded = false;
    List<Future<TitleFetch>> futures = new ArrayList<>();
    for (String username : missing) {
      futures.add(extractionExecutor.submit(() -> fetchTitle(username)));
    }
    for (Future<TitleFetch> future : futures) {
      try {
        TitleFetch fetch = future.get();
        if (fetch.failed()) {
          degraded = true;
        } else {
          cache.put(fetch.username(), fetch.title());
        }
      } catch (ExecutionException e) {
        // fetchTitle catches its own errors; this is a safety net.
        LOG.warn("Title lookup task failed", e.getCause());
        degraded = true;
      } catch (InterruptedException e) {
        Thread.currentThread().interrupt();
        LOG.warn("Interrupted while resolving player titles", e);
        degraded = true;
        break;
      }
    }
    return new TitleResolution(Collections.unmodifiableMap(new HashMap<>(cache)), degraded);
  }

  private static void collectMissingUsername(
      PlayerResult playerResult, Map<String, String> cache, Set<String> missing) {
    if (playerResult == null || playerResult.username() == null) {
      return;
    }
    String key = playerResult.username().toLowerCase();
    if (!cache.containsKey(key)) {
      missing.add(key);
    }
  }

  /**
   * Fetches a player's title from their profile. Title enrichment must never fail indexing, so any
   * lookup error (429, outage, etc.) logs and reports a failed fetch instead of throwing.
   */
  private TitleFetch fetchTitle(String username) {
    try {
      return new TitleFetch(
          username, chessClient.fetchPlayer(username).map(Player::title).orElse(null), false);
    } catch (RuntimeException e) {
      LOG.warn("Failed to fetch profile for {} while enriching titles: {}", username, e.toString());
      return new TitleFetch(username, null, true);
    }
  }

  private record TitleFetch(String username, String title, boolean failed) {}

  private record TitleResolution(Map<String, String> titles, boolean degraded) {}

  private static String titleFor(Map<String, String> titles, PlayerResult playerResult) {
    if (playerResult == null || playerResult.username() == null) {
      return null;
    }
    return titles.get(playerResult.username().toLowerCase());
  }

  /**
   * Writes a batch under this worker's lease, as one transaction.
   *
   * <p>It used to be three calls — insert games, delete the games' occurrences, insert the new ones
   * — and the middle two are a read-modify-write on rows keyed by nothing but a random UUID. Two
   * workers over the same game could interleave as delete, delete, insert, insert, and both inserts
   * survive: every motif for that game then counts double. Two live requests reaching one game is
   * ordinary, not exotic — a game has two players — so the fix has to be atomicity here rather than
   * exclusion upstream.
   */
  private void flushBatch(
      UUID requestId,
      List<GameFeature> featureBatch,
      Map<String, Map<Motif, List<GameFeatures.MotifOccurrence>>> occurrencesBatch) {
    // No early return on an empty batch. An empty flush writes nothing but still asks the question,
    // and the answer is what gates everything after it — including upsertPeriod, which is not
    // itself fenced. A month whose games were all bullet-filtered, or whose count is an exact
    // multiple of BATCH_SIZE, arrives here empty, and skipping the check there was a hole.
    if (!fenced(
        requestId,
        () ->
            gameFeatureStore.flushOwned(
                requestId, ownerId, clock.instant(), featureBatch, occurrencesBatch),
        "a batch could be written")) {
      throw new LeaseLostException(requestId, "the lease was lost before a batch could be written");
    }
    featureBatch.clear();
    occurrencesBatch.clear();
  }

  /**
   * Reports progress under the lease. A refused write means this worker no longer owns the range,
   * so it unwinds the run rather than carrying on producing rows nobody will read.
   */
  private void progress(UUID requestId, int totalIndexed) {
    if (!fenced(
        requestId,
        () ->
            requestStore.updateStatusOwned(
                requestId, ownerId, "PROCESSING", null, totalIndexed, clock.instant()),
        "progress could be recorded")) {
      throw new LeaseLostException(
          requestId, "the lease was lost before progress could be recorded");
    }
  }

  /**
   * Runs a fenced write, and retries it once if the only thing wrong was that our own lease had
   * lapsed.
   *
   * <p>A refusal has three causes and they do not deserve the same response: someone else took the
   * request, the row reached a terminal status, or this worker's lease expired with nobody taking
   * it. The last is recoverable and not rare — the renewal interval is 75 seconds against a
   * five-minute lease, but a stalled connection pool can outlast both, and the heartbeat shares one
   * scheduler thread with every other in-flight request on this instance. Treating it as a loss
   * abandons a run nobody else wants, mid-way, leaving the row PROCESSING and still holding its
   * dedupe slot until the sweep retires it with a message about an owner that never existed.
   *
   * <p>{@link IndexingRequestStore#renewLease} is exactly the discriminator: it is keyed on {@code
   * owner_id}, so it succeeds when the row still names us and fails when it does not. No separate
   * read is needed, and there is no window — a rival claim and a late renewal are two conditional
   * updates on one row, so the row lock orders them and the rival always wins.
   */
  private boolean fenced(UUID requestId, java.util.function.BooleanSupplier write, String what) {
    if (write.getAsBoolean()) {
      return true;
    }
    if (!requestStore.renewLease(requestId, ownerId, RetentionPolicy.LEASE, clock.instant())) {
      return false;
    }
    LOG.warn(
        "Lease on request {} had lapsed before {}, but no one else took it. Renewed and"
            + " continuing.",
        requestId,
        what);
    return write.getAsBoolean();
  }

  /**
   * Thrown to unwind a run whose lease has gone. Distinct from an ordinary failure because the
   * outcome is different: there is no row left for this worker to write to, and pretending
   * otherwise would corrupt the replacement's.
   */
  private static final class LeaseLostException extends RuntimeException {
    LeaseLostException(UUID requestId, String detail) {
      super("Request " + requestId + ": " + detail);
    }
  }

  private String determineResult(PlayedGame game) {
    String whiteResult = game.whiteResult() != null ? game.whiteResult().result() : null;
    String blackResult = game.blackResult() != null ? game.blackResult().result() : null;
    return ResultMapper.mapResult(whiteResult, blackResult);
  }

  private String extractEcoFromPgn(String pgn) {
    Matcher m = ECO_PATTERN.matcher(pgn);
    return m.find() ? m.group(1) : null;
  }

  private record ExtractResult(
      GameFeature row,
      String gameUrl,
      Map<Motif, List<GameFeatures.MotifOccurrence>> occurrences) {}

  private static boolean isLockConflict(Throwable e) {
    Throwable cause = e;
    while (cause != null) {
      if (cause instanceof SQLException sql) {
        String state = sql.getSQLState();
        int errorCode = sql.getErrorCode();
        // PostgreSQL: 40001 = serialization failure, 40P01 = deadlock
        // H2: 50200 = lock timeout (wraps 90131 via filterConcurrentUpdate);
        //     90131 = concurrent update (MVCC write-write conflict, directly retryable)
        if ("40001".equals(state)
            || "40P01".equals(state)
            || errorCode == 50200
            || errorCode == 90131) {
          return true;
        }
      }
      cause = cause.getCause();
    }
    return false;
  }
}
