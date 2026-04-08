package com.muchq.games.one_d4.worker;

import com.muchq.games.chess_com_client.ChessClient;
import com.muchq.games.chess_com_client.GamesResponse;
import com.muchq.games.chess_com_client.PlayedGame;
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
import java.time.Duration;
import java.util.concurrent.ExecutionException;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Future;
import java.time.Instant;
import java.time.YearMonth;
import java.time.ZoneOffset;
import java.time.format.DateTimeFormatter;
import java.util.ArrayList;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;
import java.util.Optional;
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

  public IndexWorker(
      ChessClient chessClient,
      FeatureExtractor featureExtractor,
      IndexingRequestStore requestStore,
      GameFeatureStore gameFeatureStore,
      IndexedPeriodStore periodStore,
      ExecutorService extractionExecutor) {
    this.chessClient = chessClient;
    this.featureExtractor = featureExtractor;
    this.requestStore = requestStore;
    this.gameFeatureStore = gameFeatureStore;
    this.periodStore = periodStore;
    this.extractionExecutor = extractionExecutor;
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

      for (YearMonth month = start; !month.isAfter(end); month = month.plusMonths(1)) {
        String monthStr = month.format(MONTH_FORMAT);
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

        Optional<GamesResponse> response = chessClient.fetchGames(message.player(), month);
        if (response.isEmpty()) {
          LOG.warn("No games found for player={} month={}", message.player(), month);
          continue;
        }

        List<GameFeature> featureBatch = new ArrayList<>();
        Map<String, Map<Motif, List<GameFeatures.MotifOccurrence>>> occurrencesBatch =
            new LinkedHashMap<>();

        // Submit each surviving game to the extraction pool, preserving source order.
        List<Future<ExtractResult>> futures = new ArrayList<>();
        for (PlayedGame game : response.get().games()) {
          if (message.excludeBullet() && "bullet".equals(game.timeClass())) {
            continue;
          }
          futures.add(
              extractionExecutor.submit(
                  () -> {
                    GameFeatures features = featureExtractor.extract(game.pgn());
                    GameFeature row = buildGameFeature(message, game, features);
                    return new ExtractResult(row, game.url(), features.occurrences());
                  }));
        }

        int monthCount = 0;
        for (Future<ExtractResult> future : futures) {
          ExtractResult result;
          try {
            result = future.get();
          } catch (ExecutionException e) {
            LOG.warn("Failed to index game", e.getCause());
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
        Instant fetchedAt = Instant.now();
        Instant firstDayNextMonth =
            month.plusMonths(1).atDay(1).atStartOfDay(ZoneOffset.UTC).toInstant();
        boolean isComplete = !fetchedAt.isBefore(firstDayNextMonth);
        int finalMonthCount = monthCount;
        Failsafe.with(UPSERT_RETRY)
            .run(
                () ->
                    periodStore.upsertPeriod(
                        message.player(),
                        message.platform(),
                        monthStr,
                        fetchedAt,
                        isComplete,
                        finalMonthCount,
                        message.excludeBullet()));
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

  private GameFeature buildGameFeature(
      IndexMessage message, PlayedGame game, GameFeatures features) {
    return new GameFeature(
        null, // id generated by DB
        message.requestId(),
        game.url(),
        message.platform(),
        game.whiteResult() != null ? game.whiteResult().username() : null,
        game.blackResult() != null ? game.blackResult().username() : null,
        game.whiteResult() != null ? Integer.valueOf(game.whiteResult().rating()) : null,
        game.blackResult() != null ? Integer.valueOf(game.blackResult().rating()) : null,
        game.timeClass(),
        extractEcoFromPgn(game.pgn()),
        determineResult(game),
        game.endTime(),
        features.numMoves(),
        Instant.now(),
        game.pgn());
  }

  private void flushBatch(
      List<GameFeature> featureBatch,
      Map<String, Map<Motif, List<GameFeatures.MotifOccurrence>>> occurrencesBatch) {
    if (featureBatch.isEmpty()) return;
    gameFeatureStore.insertBatch(featureBatch);
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
