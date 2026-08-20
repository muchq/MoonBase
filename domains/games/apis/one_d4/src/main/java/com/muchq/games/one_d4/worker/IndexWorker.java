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
import com.muchq.games.one_d4.openings.Openings;
import com.muchq.games.one_d4.queue.IndexMessage;
import com.muchq.platform.yodel.CustomMetrics;
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
import java.util.Locale;
import java.util.Map;
import java.util.Optional;
import java.util.Set;
import java.util.UUID;
import java.util.concurrent.CancellationException;
import java.util.concurrent.ConcurrentHashMap;
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
  private final CustomMetrics metrics;

  // Instrument names. The collector's Prometheus exporter appends _total to counters, so
  // index_runs is queried as index_runs_total. Labels are outcomes and motif names only — bounded
  // sets. A player name or a request id here would mint a new stored series per user.
  static final String RUNS = "index_runs";
  static final String GAMES_INDEXED = "games_indexed";
  static final String MONTHS = "index_months";
  static final String ARCHIVE_FETCHES = "chess_com_archive_fetches";
  static final String MOTIF_OCCURRENCES = "motif_occurrences";
  static final String RUN_DURATION = "index_run_duration_micros";

  /**
   * The label values the counters above can carry, so each series can be declared at startup and
   * export a zero baseline. Bounded sets only: {@link #MOTIF_OCCURRENCES} is labelled by motif and
   * left to appear on first use, since its baseline would be one series per motif for a tile that
   * only ever reads their sum.
   */
  static final List<String> RUN_OUTCOMES =
      List.of("completed", "failed", "interrupted", "lease_lost");

  static final List<String> MONTH_RESULTS = List.of("indexed", "degraded", "cached", "empty");

  static final List<String> ARCHIVE_FETCH_RESULTS = List.of("ok", "no_archive", "error");

  /**
   * Bounds for {@link #RUN_DURATION}, in microseconds, spanning a millisecond to the {@link
   * RetentionPolicy#MAX_RUN} ceiling.
   *
   * <p>The default set yodel shares with the HTTP latency histogram tops out at 10ms, which no
   * index run has ever finished inside — a run does at least four database round trips before it
   * fetches anything. Every observation would land in the overflow bucket: fifteen series pinned at
   * zero and a sixteenth equal to the count.
   *
   * <p>The quantile that reads them does not announce this. When the rank falls in the {@code +Inf}
   * bucket, {@code histogram_quantile} returns the upper bound of the highest <em>finite</em>
   * bucket, so a p95 over runs that all take minutes answers a flat 10000 — a plausible-looking
   * 10ms, not an obvious {@code +Inf}. That is the failure worth avoiding: a broken histogram that
   * reads like a fast one.
   */
  static final double[] RUN_DURATION_BOUNDS = {
    1_000,
    10_000,
    100_000,
    1_000_000,
    5_000_000,
    30_000_000,
    60_000_000,
    300_000_000,
    900_000_000,
    1_800_000_000,
    3_600_000_000L,
    10_800_000_000L,
    21_600_000_000L
  };

  static final String GAMES_PER_MONTH = "index_games_per_month";

  /**
   * Bounds for {@link #GAMES_PER_MONTH}, in games.
   *
   * <p>The HTTP default set happens to span a month's archive well enough — a heavy blitz player
   * clears a few hundred games and the top bound is 10,000 — so the mean this is charted through
   * reads correctly either way. Declared anyway, because "happens to" is the whole failure: the two
   * distributions would be silently coupled to a latency histogram, and the first quantile anyone
   * draws over a month count is the moment that stops being harmless.
   */
  static final double[] GAMES_PER_MONTH_BOUNDS = {
    1, 2, 5, 10, 25, 50, 100, 200, 400, 800, 1_600, 3_200
  };

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
   * Requests this process is running right now.
   *
   * <p>The lease cannot do this job, and that is not a gap in the lease — it is what the lease is.
   * {@code ownerId} names the <em>process</em> so that a run can renew across its own retries, and
   * {@link IndexingRequestStore#claim} therefore admits its own holder. Correct for the question it
   * answers ("may this process write?"), and useless for a different one ("is this process already
   * running this?").
   *
   * <p>Since #1279 there are two ways into a run and both live here: the poller, which claims from
   * the table, and {@code submitHybrid}'s inline dispatch, which hands the message straight over.
   * They can pick up the same row — the poller can claim it in the gap between {@code
   * createOrAdopt} committing and the inline call starting — and both claims succeed, because they
   * present the same token. Every fence downstream then passes for both, so the range is fetched
   * and extracted twice, one run's terminal write is refused, and nothing says so. Across two
   * processes the lease handles it; within one, only this does.
   */
  private final Set<UUID> inFlight = ConcurrentHashMap.newKeySet();

  /**
   * A snapshot of what this process is running, for the shutdown path to hand back.
   *
   * <p>A live view rather than a copy would be worse than useless here: the caller iterates it
   * while runs are still finishing and removing themselves, so it would be racing the very set it
   * is reading. Both entry points register here, so a request picked up inline is handed back on
   * shutdown just like a claimed one — the JVM is going away either way.
   */
  public Set<UUID> inFlight() {
    return Set.copyOf(inFlight);
  }

  /**
   * When each in-flight run stops being allowed to renew (#1282).
   *
   * <p>Keyed by request rather than held in a field because one process runs several at once, and
   * the two things that consult it are far apart: the heartbeat, on its own scheduler thread, and
   * {@link #fenced}, which is called from deep inside the month loop and has nothing but a request
   * id to go on.
   */
  private final Map<UUID, Instant> renewalDeadlines = new ConcurrentHashMap<>();

  /**
   * The thread each in-flight run is on, so the ceiling can stop the run rather than merely stop
   * vouching for it (#1282, option 2).
   *
   * <p>Giving the request up is only half of recovering from a wedge. The row goes back to the
   * queue and another worker gets through it, but the thread that was stuck stays stuck: the poller
   * is one thread, so the instance holding a wedged run stops taking work entirely and does not
   * start again until someone restarts it. A fleet recovering its rows one at a time while losing a
   * worker each time is not recovering.
   */
  private final Map<UUID, RunHandle> runHandles = new ConcurrentHashMap<>();

  /**
   * True once this run has been going long enough that its own liveness is no longer evidence of
   * anything. Absent means no ceiling applies — an inline run this worker never claimed, or one
   * already unwound.
   */
  private boolean pastRenewalCeiling(UUID requestId) {
    Instant deadline = renewalDeadlines.get(requestId);
    return deadline != null && !clock.instant().isBefore(deadline);
  }

  /**
   * Lets the heartbeat interrupt a run without the interrupt ever landing on a thread that has
   * moved on to something else.
   *
   * <p>Both halves lock the same handle, so the interrupt and the end of the run are ordered:
   * either the interrupt is delivered while the run is still inside {@link #claimAndRun}, where it
   * is expected and consumed, or {@link #finish} got there first and no interrupt is sent at all.
   * Without that ordering the poller would occasionally return to its loop carrying an interrupt
   * meant for a run that had already ended, and fail its next blocking call for a reason nobody
   * could reconstruct from the logs.
   *
   * <p>Package-private so the ordering can be tested directly. It is a race, and a test that had to
   * schedule one would be a test that passes when it loses.
   */
  static final class RunHandle {
    private final Thread thread;
    private boolean finished;
    private boolean interrupted;

    RunHandle(Thread thread) {
      this.thread = thread;
    }

    synchronized void interruptRun() {
      if (finished) {
        return;
      }
      interrupted = true;
      thread.interrupt();
    }

    /** Ends the run, and reports whether this worker ever interrupted it. */
    synchronized boolean finish() {
      finished = true;
      return interrupted;
    }
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
    this(
        chessClient,
        featureExtractor,
        requestStore,
        gameFeatureStore,
        periodStore,
        extractionExecutor,
        clock,
        heartbeatInterval,
        new CustomMetrics());
  }

  /**
   * @param metrics where this worker reports what it did. Defaulted to a detached registry by the
   *     constructors above rather than made nullable: recording into memory nobody exports is the
   *     same no-op yodel already gives a service with no OTEL endpoint configured, and it keeps
   *     every emit site free of a null check.
   */
  public IndexWorker(
      ChessClient chessClient,
      FeatureExtractor featureExtractor,
      IndexingRequestStore requestStore,
      GameFeatureStore gameFeatureStore,
      IndexedPeriodStore periodStore,
      ExecutorService extractionExecutor,
      Clock clock,
      Duration heartbeatInterval,
      CustomMetrics metrics) {
    this.chessClient = chessClient;
    this.featureExtractor = featureExtractor;
    this.requestStore = requestStore;
    this.gameFeatureStore = gameFeatureStore;
    this.periodStore = periodStore;
    this.extractionExecutor = extractionExecutor;
    this.clock = clock;
    this.heartbeatInterval = heartbeatInterval;
    this.metrics = metrics;
    this.heartbeatScheduler =
        Executors.newSingleThreadScheduledExecutor(
            r -> {
              Thread t = new Thread(r, "index-request-heartbeat");
              // Daemon: a heartbeat must never be the reason a JVM refuses to exit.
              t.setDaemon(true);
              return t;
            });
    declareMetrics();
  }

  /**
   * Every series this worker can report, declared at construction so each exports as 0 from process
   * start rather than springing into existence carrying its first run's numbers.
   *
   * <p>Declared here rather than at the top of {@link #process}, which is where the distribution
   * bounds used to be: a baseline is only useful if it is exported <em>before</em> the increment it
   * is a baseline for, and a run that starts and finishes between two export ticks would declare
   * and increment inside the same interval, leaving the first run as invisible as it was. The
   * worker bean is eager, so construction happens at startup and the baseline is minutes old before
   * any request arrives. See {@link CustomMetrics#defineCounter}.
   *
   * <p>The label values are spelled out because the emit sites spell them out; {@code
   * IndexWorkerTest} pins that the two sets agree, so an outcome added at an emit site without a
   * declaration here fails rather than quietly losing its first occurrence.
   */
  private void declareMetrics() {
    metrics.defineDistribution(RUN_DURATION, RUN_DURATION_BOUNDS);
    metrics.defineDistribution(GAMES_PER_MONTH, GAMES_PER_MONTH_BOUNDS);
    // Bounds are per name; the series is per name and labels, so both are declared. Without the
    // second, avg_run_seconds_1h divides a rate over one sample by a rate over one sample the
    // first time a run finishes — the same blindness as the counters, one level less obvious.
    metrics.defineDistributionSeries(GAMES_PER_MONTH);
    for (String outcome : RUN_OUTCOMES) {
      metrics.defineDistributionSeries(RUN_DURATION, Map.of("outcome", outcome));
    }

    metrics.defineCounter(GAMES_INDEXED);
    for (String outcome : RUN_OUTCOMES) {
      metrics.defineCounter(RUNS, Map.of("outcome", outcome));
    }
    for (String result : MONTH_RESULTS) {
      metrics.defineCounter(MONTHS, Map.of("result", result));
    }
    for (String result : ARCHIVE_FETCH_RESULTS) {
      metrics.defineCounter(ARCHIVE_FETCHES, Map.of("result", result));
    }
  }

  public void process(IndexMessage message) {
    LOG.info(
        "Processing request_id={} player={} platform={}",
        message.requestId(),
        message.player(),
        message.platform());

    if (!inFlight.add(message.requestId())) {
      LOG.info(
          "Skipping request_id={}: this process is already running it. The poller and an inline"
              + " dispatch both reached it; one run is enough.",
          message.requestId());
      return;
    }
    try {
      claimAndRun(message);
    } finally {
      inFlight.remove(message.requestId());
    }
  }

  private void claimAndRun(IndexMessage message) {
    if (!requestStore.claim(message.requestId(), ownerId, RetentionPolicy.LEASE, clock.instant())) {
      // Three causes, and the message must not pick one: a live rival holds the lease, the row
      // reached a terminal status, or it is gone. Only the first is benign — it means the work is
      // already owned and doing it again would put two writers on the same games.
      //
      // The second means the sweep has already given up on this request — its attempts are spent,
      // or nothing was running anywhere for long enough that the user was told it failed. Either
      // way it is not this worker's to resume.
      LOG.warn(
          "Declining request {}: it is not available to claim (already held, or retired while"
              + " queued). No games will be indexed for it.",
          message.requestId());
      return;
    }

    renewalDeadlines.put(message.requestId(), clock.instant().plus(RetentionPolicy.MAX_RUN));
    RunHandle handle = new RunHandle(Thread.currentThread());
    runHandles.put(message.requestId(), handle);
    ScheduledFuture<?> heartbeat = startHeartbeat(message.requestId());
    boolean abandonedBecauseInterrupted = false;
    // Wall-clock, not the injected clock: this measures how long the run really took, and a test
    // that jumps its clock by six hours to trip the ceiling would otherwise report a six-hour run.
    long runStartNanos = System.nanoTime();
    String outcome = "failed";
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
            metrics.increment(MONTHS, Map.of("result", "cached"));
            progress(message.requestId(), totalIndexed);
            continue;
          }
        }

        Optional<GamesResponse> response;
        try {
          response = chessClient.fetchGames(message.player(), month);
        } catch (RuntimeException e) {
          // Counted before rethrowing. ChessClient maps only a 404 to empty and throws on every
          // other non-200, so without this the rate-limits and 5xx — the failures an operator most
          // wants a rate on — were the one archive outcome the counter could not show.
          metrics.increment(ARCHIVE_FETCHES, Map.of("result", "error"));
          throw e;
        }
        // Checked on the way out of the fetch, not only where an interrupt is thrown, because a
        // blocking call is entitled to swallow one — restore the status, return normally, and
        // leave the caller unable to tell an empty archive from a call that was cut short. Taking
        // that answer at face value does two wrong things: it writes an empty period, which the
        // period cache honours until retention sweeps it a week later, and it goes on to the next
        // month, which is the opposite of stopping. This and the check before the flush below are
        // placed the same way — on the way out of a span that blocks, which is where an interrupt
        // can have been absorbed without a trace.
        ensureNotInterrupted(message.requestId());
        // ChessClient still maps every 404 to Optional.empty(). That is not an empty month: a
        // genuine empty archive is HTTP 200 with {"games":[]}. A 404 is either a missing player or
        // an upstream failure on a listed archive — both must fail the request, not COMPLETE it
        // as "indexed, no games" (#1360).
        if (response.isEmpty()) {
          metrics.increment(ARCHIVE_FETCHES, Map.of("result", "error"));
          throw new RuntimeException(
              "chess.com returned 404 for player="
                  + message.player()
                  + " month="
                  + month
                  + " (empty months are HTTP 200 with an empty games list)");
        }

        List<GameFeature> featureBatch = new ArrayList<>();
        Map<String, Map<Motif, List<GameFeatures.MotifOccurrence>>> occurrencesBatch =
            new LinkedHashMap<>();

        if (response.get().games().isEmpty()) {
          // The month *was* indexed — the answer is "none" — so record an empty period rather
          // than dropping through. Skipping it would leave the month indistinguishable from one
          // retention has swept (DataAvailabilityResolver reads a missing row as "gone"), and
          // would make the period cache miss forever, refetching an empty archive on every
          // request.
          LOG.warn("No games found for player={} month={}", message.player(), month);
          // Ownership first. upsertPeriod is the one write in a run that carries no token —
          // indexed_periods is keyed by (player, platform, month) and has no request to fence
          // against — so the only protection it can have is not reaching it after the lease is
          // gone. Writing an empty period for a month a replacement has already indexed properly
          // would make the period cache report zero games and skip that month until retention
          // sweeps it.
          progress(message.requestId(), totalIndexed);
          upsertPeriod(message, monthStr, month, fetchedAt, 0, false);
          // Counted as an empty month, and deliberately not recorded into
          // GAMES_PER_MONTH: a decade-long backfill of a three-year player is mostly
          // empty archives, and feeding those zeros in makes the average archive look a
          // third its real size. empty_months_total already carries that population.
          metrics.increment(MONTHS, Map.of("result", "empty"));
          // no_archive kept as the empty-games label so existing dashboard selectors still match;
          // the fetch itself succeeded (HTTP 200).
          metrics.increment(ARCHIVE_FETCHES, Map.of("result", "no_archive"));
          continue;
        }
        metrics.increment(ARCHIVE_FETCHES, Map.of("result", "ok"));

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
        // The third span that blocks, and it swallows the interrupt exactly like the fetch above:
        // it marks itself degraded and returns a usable map, because a title lookup must never
        // fail indexing. Walking on from here is worse than it looks. The drain below cannot catch
        // the interrupt for us — a future that has already completed returns from get() without
        // ever checking the flag — so every game is submitted to the shared pool, and a month at
        // BATCH_SIZE flushes a partial batch mid-loop. That flush is fenced but not refused: the
        // ceiling stops the heartbeat, it does not expire the lease, so for the next LEASE this
        // run still owns the row and the write lands. Stop before anything is submitted.
        ensureNotInterrupted(message.requestId());
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
        try {
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
            // Counted here rather than at flush time because the flush batches by request and
            // loses the per-motif breakdown. The motif name is the label, and the detector set is
            // fixed and small, so the cardinality is bounded by the code rather than by traffic.
            result
                .occurrences()
                .forEach(
                    (motif, occurrences) ->
                        metrics.add(
                            MOTIF_OCCURRENCES,
                            occurrences.size(),
                            Map.of("motif", motif.name().toLowerCase(Locale.ROOT))));
            monthCount++;
            totalIndexed++;
            if (featureBatch.size() >= BATCH_SIZE) {
              flushBatch(message.requestId(), featureBatch, occurrencesBatch);
              progress(message.requestId(), totalIndexed);
            }
          }
        } finally {
          abandon(futures);
        }
        // The drain above stops early when this thread is interrupted, and everything below it
        // writes — a partial batch, then a period row stamping the month as indexed. That row is
        // the damaging one: it is keyed by (player, platform, month) with no request to fence it,
        // so a month half-extracted before the run was stopped would be cached as done and skipped
        // by every later request until retention swept it.
        ensureNotInterrupted(message.requestId());
        flushBatch(message.requestId(), featureBatch, occurrencesBatch);
        if (titleResolution.degraded()) {
          LOG.warn(
              "Title enrichment degraded for player={} month={}; period stored incomplete",
              message.player(),
              monthStr);
        }
        // Unfenced, like the empty-archive path above and for the same reason — indexed_periods is
        // keyed by
        // (player, platform, month), so there is no request to condition the write on. What keeps
        // it safe is order: the flushBatch immediately above checks ownership and throws if this
        // worker has lost it, so this line is unreachable without a live lease.
        upsertPeriod(message, monthStr, month, fetchedAt, monthCount, titleResolution.degraded());
        metrics.increment(
            MONTHS, Map.of("result", titleResolution.degraded() ? "degraded" : "indexed"));
        metrics.add(GAMES_INDEXED, monthCount, Map.of());
        metrics.record(GAMES_PER_MONTH, monthCount);
        progress(message.requestId(), totalIndexed);
      }

      final int finalCount = totalIndexed;
      if (fenced(
          message.requestId(),
          () ->
              requestStore.updateStatusOwned(
                  message.requestId(), ownerId, "COMPLETED", null, finalCount, clock.instant()),
          "the run could be completed")) {
        LOG.info("Completed request_id={} games={}", message.requestId(), totalIndexed);
        outcome = "completed";
      } else {
        // Logging the count unconditionally read as success whatever the row said. The games are
        // on disk either way; what is lost is the record that this run produced them.
        LOG.warn(
            "Indexed {} games for request {} but could not record COMPLETED: the lease is gone.",
            totalIndexed,
            message.requestId());
        // The games landed but the run cannot claim them. Distinct from "completed" on purpose:
        // counting it as success would hide a range changing hands mid-run behind a green chart.
        outcome = "lease_lost";
      }
    } catch (LeaseLostException e) {
      // No terminal write is attempted, and that is the whole difference from an ordinary failure.
      // It is not that a FAILED here would damage the replacement's row — that write is fenced
      // too, and would simply be refused. It is that attempting it would log a spurious error for
      // a run that did not fail: it was stopped, correctly, because the range moved.
      LOG.warn("Abandoning request_id={} reason={}", message.requestId(), e.getMessage());
      outcome = "lease_lost";
    } catch (Exception e) {
      if (e instanceof RunInterruptedException || Thread.currentThread().isInterrupted()) {
        // Stopped, not failed, and the row's outcome turns on the difference. A FAILED here spends
        // one of three attempts and tells the user their range is broken, when what actually
        // happened is that this worker was told to let go of it. Left alone, the request goes back
        // to the queue and a worker that is not wedged finishes it.
        //
        // Checked on the status rather than only on the exception type because an interrupt
        // arrives in whatever shape the blocking call gives it — an interrupted HTTP read, a
        // driver's own wrapper, a future that was being waited on. What they have in common is the
        // status, which every one of them restores on the way out.
        LOG.warn(
            "Abandoning request {}: this run was interrupted before it finished, so it is being"
                + " left for another worker rather than recorded as a failure.",
            message.requestId(),
            e);
        abandonedBecauseInterrupted = true;
        outcome = "interrupted";
      } else {
        recordFailure(message.requestId(), e);
      }
    } finally {
      heartbeat.cancel(false);
      renewalDeadlines.remove(message.requestId());
      runHandles.remove(message.requestId());
      boolean weInterruptedThisRun = handle.finish();
      if (weInterruptedThisRun) {
        // Our own interrupt, and it stops here. The thread it landed on belongs to the poller,
        // which has more requests to claim after this one; letting the status escape would make
        // its next blocking call fail for a reason that has nothing to do with the request it is
        // working on by then. An interrupt from anywhere else is left alone — this worker did not
        // send it and does not know what it means.
        Thread.interrupted();
      }
      // Here rather than in the interrupt catch above, because that catch is not the only way out
      // of an interrupted run and was not even the common one. Past the ceiling every fenced write
      // refuses without renewing, so the run unwinds as a LeaseLostException — caught first, and
      // by a branch that knows nothing about interrupts. It leaves owner_id set, and the poller
      // that comes back for the row is the owner named in it, so claim holds attempts flat and the
      // wedge repeats for free. The cached-month progress call reaches the same place with no
      // checkpoint in front of it at all.
      //
      // Ordered after the status is cleared: releasing is a database write, and handing it a
      // thread that is still flagged invites the driver to fail it for the wrong reason.
      //
      // The two terms overlap without either containing the other. The first is true whenever our
      // ceiling fired, however the run then unwound — including the LeaseLostException route,
      // which never reaches the catch that sets the second. The second is true whenever the run
      // unwound as interrupted, including on an interrupt this worker did not send, where the
      // first is false.
      if (weInterruptedThisRun || abandonedBecauseInterrupted) {
        releaseAfterInterrupt(message.requestId());
        // A run the ceiling cut loose reports as interrupted even when it unwound through the
        // LeaseLostException branch above, which is the shape a past-ceiling fenced refusal takes.
        // Reporting those as lease_lost would bury the wedge signal in ordinary range handovers.
        outcome = "interrupted";
      }
      metrics.increment(RUNS, Map.of("outcome", outcome));
      // Labelled by outcome, like the counter beside it. An interrupted run sat at the MAX_RUN
      // ceiling by definition, so pooling it with the completed ones makes the average run time
      // jump precisely when a wedge is being cut loose — the tile reads worst exactly when it is
      // being used to judge how bad things are. Four outcomes over these bounds is 64 series.
      metrics.record(
          RUN_DURATION, (System.nanoTime() - runStartNanos) / 1000.0, Map.of("outcome", outcome));
    }
  }

  /**
   * Gives up the row an interrupted run was holding, so the next claim on it counts.
   *
   * <p>Leaving it owned looks harmless — the lease lapses on its own within {@link
   * RetentionPolicy#LEASE} and any worker may then take it — and it is the one thing that turns
   * this whole ceiling back into a no-op. {@code claim} holds {@code attempts} flat when the owner
   * re-presents the same token, so the poller that just abandoned this run re-claims its own row a
   * few minutes later, takes a fresh {@link RetentionPolicy#MAX_RUN}, and wedges again on a counter
   * that has not moved. Nothing ever retires it, and for as long as it loops its live lease answers
   * "yes, someone is working" on behalf of the entire fleet. The bound this class advertises would
   * exist and bound nothing.
   *
   * <p>Not through {@link #fenced}: that renews on a refused write, which is the exact opposite of
   * letting go. Best-effort — if the database is the reason this run wedged, the hourly release arm
   * clears the owner once the lease lapses and the next claim counts then instead.
   */
  private void releaseAfterInterrupt(UUID requestId) {
    try {
      requestStore.releaseOwned(requestId, ownerId, clock.instant());
    } catch (RuntimeException e) {
      LOG.warn(
          "Could not release request {} after its run was cut loose; the hourly sweep will clear"
              + " the owner once the lease lapses.",
          requestId,
          e);
    }
  }

  /**
   * Records a run that genuinely failed.
   *
   * <p>Through fenced(), like every other write in the run. This is the write most likely to be
   * refused for a reason unrelated to what it is reporting — the database problem that throws out
   * of a run is the same one that stalls the heartbeat — and it is the only write that can explain
   * the failure. Refused, it leaves the row PROCESSING holding its dedupe slot, so the range stays
   * blocked until the hourly sweep retires it under a message about an owner that stopped
   * responding, which is not what happened.
   */
  private void recordFailure(UUID requestId, Exception cause) {
    LOG.error("Failed to process request_id={}", requestId, cause);
    if (!fenced(
        requestId,
        () ->
            requestStore.updateStatusOwned(
                requestId,
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
          requestId);
    }
  }

  /**
   * Unwinds the run if this thread has been told to stop.
   *
   * <p>Called where the run is about to commit to more work or to a write, which is where the
   * difference between stopping and carrying on becomes visible from outside the process.
   */
  private static void ensureNotInterrupted(UUID requestId) {
    if (Thread.currentThread().isInterrupted()) {
      throw new RunInterruptedException(requestId);
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
            if (pastRenewalCeiling(requestId)) {
              // Loudly, and at ERROR: nothing legitimate reaches this. A twelve-month range for a
              // prolific player is minutes against a healthy chess.com, so a run still going after
              // MAX_RUN is a wedge — a hung socket, a deadlocked pool, an unbounded retry — and
              // the request is about to cost an attempt because of it. Whoever reads this needs to
              // go and look at the worker, not at the request.
              LOG.error(
                  "Request {} has been running for {} without finishing. Releasing the lease and"
                      + " interrupting the run: a run this long is a stuck worker, and holding it"
                      + " open strands this range and hides every other queued request from the"
                      + " staleness sweep.",
                  requestId,
                  RetentionPolicy.MAX_RUN);
              interruptRun(requestId);
              throw new CancellationException("run exceeded the renewal ceiling");
            }
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
            LOG.warn("Heartbeat failed request_id={}", requestId, e);
          }
        },
        everyMillis,
        everyMillis,
        TimeUnit.MILLISECONDS);
  }

  /**
   * Lets go of work this run submitted to the extraction pool, interrupting whatever is still
   * running.
   *
   * <p>Interrupting the run thread frees the run; it does nothing for the pool threads the run left
   * behind, and those are the ones holding chess.com connections. Title resolution submits a
   * profile lookup per distinct opponent, so the same hung peer that wedged the archive fetch
   * wedges every one of them — and unlike the run, they belong to a pool shared by every request on
   * this instance. Leaving them there recovers one worker's poll loop while quietly retiring its
   * extraction capacity, which is the same failure a size down.
   *
   * <p>A no-op on the ordinary path: the drain loops consume every future before they return, and
   * cancelling a completed future does nothing. This only bites when a loop broke out early.
   */
  private static void abandon(List<? extends Future<?>> futures) {
    for (Future<?> future : futures) {
      future.cancel(true);
    }
  }

  /**
   * Cuts a run loose from whatever it is blocked on, so this worker gets its slot back.
   *
   * <p>Releasing the lease is what recovers the <em>request</em>; this is what recovers the
   * <em>worker</em>. Without it a wedged run keeps its thread forever, and since the poller is one
   * thread, the instance stops taking work until someone restarts it — so the fleet recovers its
   * rows one at a time while shedding a worker for each one.
   *
   * <p>An interrupt only ends a run if what the run is blocked on honours it. The calls this worker
   * actually waits in are JDK {@code HttpClient} sends and body reads, and both return promptly
   * when the thread is interrupted (#1282).
   *
   * <p>The unwind keys on the interrupt <em>status</em>, and the two paths leave it set for
   * different reasons — worth naming, because only one of them is the JDK's doing. A body read
   * surfaces as an {@code IOException} wrapping {@code InterruptedException} with the status
   * already set. A {@code send} throws {@code InterruptedException} with the status
   * <em>cleared</em>, and {@code Jdk11HttpClient} is what restores it before rethrowing. That
   * restore is load-bearing for this class, not tidiness: drop it and a wedged send unwinds as an
   * ordinary failure, which spends an attempt and blames the range.
   *
   * <p>Where the wedge is somewhere that honours nothing — a lock held forever by another thread, a
   * native call — this changes nothing and the ceiling still gives the request up. The two are
   * independent on purpose: the row is recovered by the lease lapsing, not by the interrupt
   * landing.
   *
   * <p>Absent from the map means the run already ended. Nothing to stop, and nothing to interrupt:
   * the thread has moved on and the interrupt would land on whatever it is doing now.
   */
  private void interruptRun(UUID requestId) {
    RunHandle handle = runHandles.get(requestId);
    if (handle != null) {
      handle.interruptRun();
    }
  }

  /**
   * Records that this month was indexed, whatever it turned out to contain.
   *
   * <p>Every fetched month gets a row, including one whose archive was empty — a missing row means
   * "not indexed", and both the period cache and {@code DataAvailabilityResolver} read it that way.
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
    try {
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
    } finally {
      abandon(futures);
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
    if (pastRenewalCeiling(requestId)) {
      // The recovery above is exactly wrong here, and this is the half of #1282 that is easy to
      // miss. Nobody has necessarily taken this row — the heartbeat gave it up on purpose — so
      // owner_id still names us and the renewal below would succeed, restoring the lease the
      // ceiling just took away and putting the run straight back to holding the range. Capping
      // only the heartbeat looks like a fix and is not one.
      LOG.warn(
          "Not renewing request {} so that {}: this run is past the {} ceiling and has given the"
              + " request up.",
          requestId,
          what,
          RetentionPolicy.MAX_RUN);
      return false;
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

  /**
   * Thrown to unwind a run that has been told to stop. A sibling of {@link LeaseLostException} and
   * for the same reason: neither is a failure of the range, so neither may leave a FAILED behind
   * for a user to read as one.
   */
  private static final class RunInterruptedException extends RuntimeException {
    RunInterruptedException(UUID requestId) {
      super("Request " + requestId + ": the run was interrupted and will not continue");
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
