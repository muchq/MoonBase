package com.muchq.games.one_d4;

import com.fasterxml.jackson.databind.ObjectMapper;
import com.muchq.games.chess_com_client.ChessClient;
import com.muchq.games.chessql.compiler.SqlCompiler;
import com.muchq.games.one_d4.db.DataSourceFactory;
import com.muchq.games.one_d4.db.GameFeatureDao;
import com.muchq.games.one_d4.db.GameFeatureStore;
import com.muchq.games.one_d4.db.IndexedPeriodDao;
import com.muchq.games.one_d4.db.IndexedPeriodStore;
import com.muchq.games.one_d4.db.IndexingRequestDao;
import com.muchq.games.one_d4.db.IndexingRequestStore;
import com.muchq.games.one_d4.db.Migration;
import com.muchq.games.one_d4.engine.FeatureExtractor;
import com.muchq.games.one_d4.engine.GameReplayer;
import com.muchq.games.one_d4.engine.PgnParser;
import com.muchq.games.one_d4.motifs.Detectors;
import com.muchq.games.one_d4.motifs.MotifDetector;
import com.muchq.games.one_d4.queue.InMemoryIndexQueue;
import com.muchq.games.one_d4.queue.IndexQueue;
import com.muchq.games.one_d4.service.DataAvailabilityResolver;
import com.muchq.games.one_d4.service.IndexRequestService;
import com.muchq.games.one_d4.service.PositionAnalyzer;
import com.muchq.games.one_d4.worker.IndexWorker;
import com.muchq.games.one_d4.worker.IndexWorkerLifecycle;
import com.muchq.platform.http_client.core.HttpClient;
import com.muchq.platform.http_client.jdk.Jdk11HttpClient;
import com.muchq.platform.json.JsonUtils;
import com.muchq.platform.yodel.CustomMetrics;
import io.micronaut.context.annotation.Bean;
import io.micronaut.context.annotation.Context;
import io.micronaut.context.annotation.Factory;
import io.micronaut.context.annotation.Value;
import java.time.Clock;
import java.util.List;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;
import java.util.concurrent.ThreadFactory;
import java.util.concurrent.atomic.AtomicInteger;
import javax.sql.DataSource;
import org.jdbi.v3.core.Jdbi;
import org.jspecify.annotations.Nullable;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

@Factory
public class IndexerModule {
  private static final Logger LOG = LoggerFactory.getLogger(IndexerModule.class);

  /**
   * The JDBC URL, from {@code $INDEXER_DB_URL}. The variable is the only source and there is no
   * default, so an unset or blank one is fatal.
   *
   * <p>Failing here is the point. Any fallback a service can start on unattended — an in-memory
   * database, a file on the host — turns a misconfigured URL into a container that boots, serves,
   * answers {@code /health} 200, and loses every write on restart. That is silent data loss where
   * an outage is the correct answer, and this exception is the outage.
   *
   * <p>Naming H2 as a default would not even reach that far: the driver is a test dependency and is
   * not on this classpath ({@code IndexerModuleTest.h2IsNotOnTheProductionClasspath}), so it would
   * fail at pool construction with "No suitable driver" instead of with the variable's name.
   *
   * <p>Local development needs a real Postgres and a real URL, the same as the deploy. See the
   * README.
   */
  static String readJdbcUrl() {
    return readJdbcUrl(System.getenv("INDEXER_DB_URL"));
  }

  static String readJdbcUrl(@Nullable String envUrl) {
    if (envUrl == null || envUrl.isBlank()) {
      throw new IllegalStateException(
          "INDEXER_DB_URL is not set. one_d4 needs a PostgreSQL JDBC URL"
              + " (jdbc:postgresql://host:5432/db); there is no in-memory fallback, because a"
              + " service that starts on one loses every write on restart without saying so.");
    }
    return envUrl.strip();
  }

  /** One clock for everything that stamps or compares retention timestamps. */
  @Context
  public Clock clock() {
    return Clock.systemUTC();
  }

  @Context
  public ObjectMapper objectMapper() {
    return JsonUtils.mapper();
  }

  @Context
  public HttpClient httpClient() {
    return new Jdk11HttpClient(java.net.http.HttpClient.newHttpClient());
  }

  @Context
  public ChessClient chessClient(HttpClient httpClient, ObjectMapper objectMapper) {
    return new ChessClient(httpClient, objectMapper);
  }

  /**
   * @param configuredUrl the {@code indexer.db.url} property. Tests set it to give each
   *     ApplicationContext its own H2 database, which is the only place H2 is reachable from — the
   *     driver is a test dependency. It has to be read here rather than assumed: a context whose
   *     property is ignored silently shares one database with every other context. Nothing sets it
   *     in production, where the URL comes from {@code $INDEXER_DB_URL}.
   */
  @Context
  public DataSource dataSource(@Value("${indexer.db.url:}") String configuredUrl) {
    return DataSourceFactory.create(
        jdbcUrl(configuredUrl),
        System.getenv("INDEXER_DB_USERNAME"),
        System.getenv("INDEXER_DB_PASSWORD"));
  }

  /**
   * Which SQL dialect the DAOs and migrations speak. It is a test concern in practice — the only
   * URLs that answer true here are the ones tests set through {@code indexer.db.url}, since the H2
   * driver is not on the production classpath — but the branches it selects live in production code
   * because the DAOs those tests exercise are the production DAOs.
   */
  @Context
  public Boolean useH2(@Value("${indexer.db.url:}") String configuredUrl) {
    return jdbcUrl(configuredUrl).contains(":h2:");
  }

  private static String jdbcUrl(@Nullable String configuredUrl) {
    return configuredUrl == null || configuredUrl.isBlank() ? readJdbcUrl() : configuredUrl.strip();
  }

  @Context
  public Migration migration(DataSource dataSource, Boolean useH2) {
    Migration migration = new Migration(dataSource, useH2);
    migration.run();
    return migration;
  }

  @Context
  public Jdbi jdbi(DataSource dataSource) {
    return Jdbi.create(dataSource);
  }

  @Context
  public IndexingRequestStore indexingRequestStore(Jdbi jdbi, Clock clock) {
    return new IndexingRequestDao(jdbi, clock);
  }

  @Context
  public GameFeatureStore gameFeatureStore(Jdbi jdbi, Boolean useH2) {
    return new GameFeatureDao(jdbi, useH2);
  }

  @Context
  public IndexedPeriodStore indexedPeriodStore(Jdbi jdbi, Boolean useH2) {
    return new IndexedPeriodDao(jdbi, useH2);
  }

  @Context
  public IndexQueue indexQueue() {
    return new InMemoryIndexQueue();
  }

  @Context
  public SqlCompiler sqlCompiler() {
    return new SqlCompiler();
  }

  @Context
  public IndexRequestService indexRequestService(
      IndexingRequestStore requestStore,
      IndexQueue queue,
      IndexWorker worker,
      DataAvailabilityResolver dataAvailability,
      Clock clock) {
    return new IndexRequestService(requestStore, queue, worker::process, dataAvailability, clock);
  }

  @Context
  public DataAvailabilityResolver dataAvailabilityResolver(IndexedPeriodStore periodStore) {
    return new DataAvailabilityResolver(periodStore);
  }

  @Context
  public List<MotifDetector> motifDetectors() {
    return Detectors.defaultDetectors();
  }

  @Context
  public PgnParser pgnParser() {
    return new PgnParser();
  }

  @Context
  public GameReplayer gameReplayer() {
    return new GameReplayer();
  }

  @Context
  public FeatureExtractor featureExtractor(
      PgnParser pgnParser, GameReplayer replayer, List<MotifDetector> detectors) {
    return new FeatureExtractor(pgnParser, replayer, detectors);
  }

  private static final AtomicInteger EXTRACT_THREAD_COUNTER = new AtomicInteger();

  static int parseThreads(@Nullable String raw, int defaultValue) {
    return parseThreads("INDEXER_EXTRACTION_THREADS", raw, defaultValue);
  }

  /**
   * @param setting the environment variable the value came from. Named rather than hardcoded
   *     because two pools parse their size this way now, and a warning that points an operator at
   *     the variable they did not set is worse than no warning.
   */
  static int parseThreads(String setting, @Nullable String raw, int defaultValue) {
    if (raw == null || raw.isBlank()) {
      return defaultValue;
    }
    try {
      int parsed = Integer.parseInt(raw.strip());
      if (parsed <= 0) {
        LOG.warn("Invalid {}={}; falling back to {}", setting, raw, defaultValue);
        return defaultValue;
      }
      return parsed;
    } catch (NumberFormatException e) {
      LOG.warn("Unparseable {}={}; falling back to {}", setting, raw, defaultValue);
      return defaultValue;
    }
  }

  @Context
  @Bean(preDestroy = "shutdown")
  @jakarta.inject.Named("indexExtraction")
  public ExecutorService indexExtractionExecutor() {
    int threads = parseThreads(System.getenv("INDEXER_EXTRACTION_THREADS"), 4);
    ThreadFactory tf =
        r -> {
          Thread t = new Thread(r);
          t.setName("index-extract-" + EXTRACT_THREAD_COUNTER.incrementAndGet());
          t.setDaemon(true);
          return t;
        };
    LOG.info("Index extraction executor: fixed pool of {} threads", threads);
    return Executors.newFixedThreadPool(threads, tf);
  }

  private static final AtomicInteger ANALYZE_THREAD_COUNTER = new AtomicInteger();

  /**
   * Its own pool, not the indexing one. /v1/analyze is a synchronous request path and indexing is a
   * background batch: sharing would let a backlog of extraction work queue ahead of an interactive
   * analysis and burn its timeout waiting to start, which would read as the analyzer being slow.
   */
  @Context
  @Bean(preDestroy = "shutdown")
  @jakarta.inject.Named("positionAnalysis")
  public ExecutorService positionAnalysisExecutor() {
    int threads = parseThreads("ANALYZE_THREADS", System.getenv("ANALYZE_THREADS"), 2);
    ThreadFactory tf =
        r -> {
          Thread t = new Thread(r);
          t.setName("analyze-" + ANALYZE_THREAD_COUNTER.incrementAndGet());
          t.setDaemon(true);
          return t;
        };
    LOG.info("Position analysis executor: fixed pool of {} threads", threads);
    return Executors.newFixedThreadPool(threads, tf);
  }

  @Context
  public PositionAnalyzer positionAnalyzer(
      FeatureExtractor featureExtractor,
      @jakarta.inject.Named("positionAnalysis") ExecutorService analysisPool) {
    long timeoutMillis = parseTimeoutMillis(System.getenv("ANALYZE_TIMEOUT_MILLIS"), 10_000L);
    LOG.info("Position analyzer: {}ms ceiling per request", timeoutMillis);
    return new PositionAnalyzer(featureExtractor, analysisPool, timeoutMillis);
  }

  static long parseTimeoutMillis(String raw, long defaultValue) {
    if (raw == null || raw.isBlank()) {
      return defaultValue;
    }
    try {
      long parsed = Long.parseLong(raw.strip());
      if (parsed <= 0) {
        LOG.warn("Invalid ANALYZE_TIMEOUT_MILLIS={}; falling back to {}", raw, defaultValue);
        return defaultValue;
      }
      return parsed;
    } catch (NumberFormatException e) {
      LOG.warn("Unparseable ANALYZE_TIMEOUT_MILLIS={}; falling back to {}", raw, defaultValue);
      return defaultValue;
    }
  }

  /**
   * @param clock the same bean every other lease participant gets. The worker was the one holdout,
   *     falling back to {@code Clock.systemUTC()} inside its no-clock constructor. Both resolve to
   *     the same thing today, so nothing was broken — but the worker now stamps {@code
   *     lease_expires_at} and {@code reclaimStale} compares against it, so a divergence that used
   *     to nudge a seven-day retention boundary would decide ownership on a five-minute window.
   */
  @Context
  public IndexWorker indexWorker(
      ChessClient chessClient,
      FeatureExtractor featureExtractor,
      IndexingRequestStore requestStore,
      GameFeatureStore gameFeatureStore,
      IndexedPeriodStore periodStore,
      @jakarta.inject.Named("indexExtraction") ExecutorService indexExtractionExecutor,
      Clock clock,
      CustomMetrics metrics) {
    return new IndexWorker(
        chessClient,
        featureExtractor,
        requestStore,
        gameFeatureStore,
        periodStore,
        indexExtractionExecutor,
        clock,
        IndexWorker.DEFAULT_HEARTBEAT_INTERVAL,
        metrics);
  }

  // preDestroy is load-bearing, not hygiene: the poller is a daemon thread, so without it a deploy
  // is indistinguishable from a crash — the row stays owned by a dead process for a full lease, and
  // the attempt it spent is gone. See IndexWorkerLifecycle#stop.
  @Context
  @Bean(preDestroy = "stop")
  public IndexWorkerLifecycle indexWorkerLifecycle(
      IndexQueue queue, IndexWorker worker, IndexingRequestStore requestStore, Clock clock) {
    return new IndexWorkerLifecycle(queue, worker, requestStore, clock);
  }
}
