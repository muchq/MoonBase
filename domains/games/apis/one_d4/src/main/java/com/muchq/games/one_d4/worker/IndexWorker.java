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
import java.util.concurrent.ExecutionException;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Future;
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

  /**
   * @param clock the source for {@code indexed_periods.fetched_at}, which retention compares
   *     against. Injected so a test can advance time across the retention boundary instead of
   *     backdating rows, which only ever exercises {@code deleteOlderThan} and never the window
   *     arithmetic that decides what "old" means.
   */
  public IndexWorker(
      ChessClient chessClient,
      FeatureExtractor featureExtractor,
      IndexingRequestStore requestStore,
      GameFeatureStore gameFeatureStore,
      IndexedPeriodStore periodStore,
      ExecutorService extractionExecutor,
      Clock clock) {
    this.chessClient = chessClient;
    this.featureExtractor = featureExtractor;
    this.requestStore = requestStore;
    this.gameFeatureStore = gameFeatureStore;
    this.periodStore = periodStore;
    this.extractionExecutor = extractionExecutor;
    this.clock = clock;
  }

  public void process(IndexMessage message) {
    LOG.info(
        "Processing index request {} for player={} platform={}",
        message.requestId(),
        message.player(),
        message.platform());

    try {
      requestStore.updateStatus(message.requestId(), "PROCESSING", null, 0);

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
            requestStore.updateStatus(message.requestId(), "PROCESSING", null, totalIndexed);
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
          upsertPeriod(message, monthStr, month, fetchedAt, 0, false);
          requestStore.updateStatus(message.requestId(), "PROCESSING", null, totalIndexed);
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
            flushBatch(featureBatch, occurrencesBatch);
            requestStore.updateStatus(message.requestId(), "PROCESSING", null, totalIndexed);
          }
        }
        flushBatch(featureBatch, occurrencesBatch);
        if (titleResolution.degraded()) {
          LOG.warn(
              "Title enrichment degraded for player={} month={}; period stored incomplete",
              message.player(),
              monthStr);
        }
        upsertPeriod(message, monthStr, month, fetchedAt, monthCount, titleResolution.degraded());
        requestStore.updateStatus(message.requestId(), "PROCESSING", null, totalIndexed);
      }

      requestStore.updateStatus(message.requestId(), "COMPLETED", null, totalIndexed);
      LOG.info("Completed indexing request {} with {} games", message.requestId(), totalIndexed);
    } catch (Exception e) {
      LOG.error("Failed to process index request {}", message.requestId(), e);
      requestStore.updateStatus(
          message.requestId(), "FAILED", "Indexing failed due to an internal error", 0);
    }
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

  private void flushBatch(
      List<GameFeature> featureBatch,
      Map<String, Map<Motif, List<GameFeatures.MotifOccurrence>>> occurrencesBatch) {
    if (featureBatch.isEmpty()) return;
    gameFeatureStore.insertBatch(featureBatch);
    gameFeatureStore.deleteOccurrencesByGameUrls(new ArrayList<>(occurrencesBatch.keySet()));
    gameFeatureStore.insertOccurrencesBatch(occurrencesBatch);
    featureBatch.clear();
    occurrencesBatch.clear();
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
