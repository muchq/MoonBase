package com.muchq.games.mcpserver;

import com.fasterxml.jackson.databind.ObjectMapper;
import com.muchq.games.chess_com_client.ChessClient;
import com.muchq.games.chessql.compiler.SqlCompiler;
import com.muchq.games.mcpserver.tools.IndexerFacade;
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
import com.muchq.games.one_d4.queue.InMemoryIndexQueue;
import com.muchq.games.one_d4.queue.IndexQueue;
import com.muchq.games.one_d4.service.DataAvailabilityResolver;
import com.muchq.games.one_d4.service.IndexRequestService;
import com.muchq.games.one_d4.worker.IndexWorker;
import com.muchq.games.one_d4.worker.IndexWorkerLifecycle;
import com.muchq.platform.http_client.core.HttpClient;
import com.muchq.platform.http_client.jdk.Jdk11HttpClient;
import com.muchq.platform.json.JsonUtils;
import io.micronaut.context.annotation.Bean;
import io.micronaut.context.annotation.Context;
import io.micronaut.context.annotation.Factory;
import java.time.Clock;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;
import java.util.concurrent.ThreadFactory;
import java.util.concurrent.atomic.AtomicInteger;
import javax.sql.DataSource;
import org.jdbi.v3.core.Jdbi;

@Factory
public class McpModule {

  // In-process indexer (one_d4 MCP_INTEGRATION Option A): H2 in-memory by default, or an
  // external database via INDEXER_DB_URL for durable indexes.
  private static final String DEFAULT_JDBC_URL = "jdbc:h2:mem:indexer;DB_CLOSE_DELAY=-1";

  // Deliberately not the injected Micronaut ObjectMapper: chess.com responses carry fields this
  // client does not model, and JsonUtils' mapper is the one configured to ignore them. Tool
  // payloads get the same treatment for the same reason — see ToolJson.
  private static final ObjectMapper CHESS_CLIENT_MAPPER = JsonUtils.mapper();

  static String indexerJdbcUrl() {
    String env = System.getenv("INDEXER_DB_URL");
    return env != null && !env.isBlank() ? env.strip() : DEFAULT_JDBC_URL;
  }

  @Context
  public Clock clock() {
    return Clock.systemUTC();
  }

  @Context
  public HttpClient httpClient() {
    return new Jdk11HttpClient(java.net.http.HttpClient.newHttpClient());
  }

  @Context
  public ChessClient chessClient(HttpClient httpClient) {
    return new ChessClient(httpClient, CHESS_CLIENT_MAPPER);
  }

  @Context
  public DataSource dataSource() {
    return DataSourceFactory.create(indexerJdbcUrl());
  }

  @Context
  public Migration migration(DataSource dataSource) {
    Migration migration = new Migration(dataSource, indexerJdbcUrl().contains(":h2:"));
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
  public GameFeatureStore gameFeatureStore(Jdbi jdbi) {
    return new GameFeatureDao(jdbi, indexerJdbcUrl().contains(":h2:"));
  }

  @Context
  public IndexedPeriodStore indexedPeriodStore(Jdbi jdbi) {
    return new IndexedPeriodDao(jdbi, indexerJdbcUrl().contains(":h2:"));
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
  public FeatureExtractor featureExtractor() {
    // Same detector set as one_d4's IndexerModule — indexes built by either process must agree
    return new FeatureExtractor(new PgnParser(), new GameReplayer(), Detectors.defaultDetectors());
  }

  private static final AtomicInteger EXTRACT_THREAD_COUNTER = new AtomicInteger();
  private static final AtomicInteger LOOKUP_THREAD_COUNTER = new AtomicInteger();

  @Context
  @Bean(preDestroy = "shutdown")
  @jakarta.inject.Named("indexExtraction")
  public ExecutorService indexExtractionExecutor() {
    ThreadFactory tf =
        r -> {
          Thread t = new Thread(r);
          t.setName("index-extract-" + EXTRACT_THREAD_COUNTER.incrementAndGet());
          t.setDaemon(true);
          return t;
        };
    return Executors.newFixedThreadPool(4, tf);
  }

  // Bounded pool for chess.com profile lookups from chess_com_players: batch calls run
  // concurrently but total fan-out against chess.com is capped at the pool size.
  @Context
  @Bean(preDestroy = "shutdown")
  @jakarta.inject.Named("chessComLookup")
  public ExecutorService chessComLookupExecutor() {
    ThreadFactory tf =
        r -> {
          Thread t = new Thread(r);
          t.setName("chess-lookup-" + LOOKUP_THREAD_COUNTER.incrementAndGet());
          t.setDaemon(true);
          return t;
        };
    return Executors.newFixedThreadPool(4, tf);
  }

  @Context
  public IndexWorker indexWorker(
      ChessClient chessClient,
      FeatureExtractor featureExtractor,
      IndexingRequestStore requestStore,
      GameFeatureStore gameFeatureStore,
      IndexedPeriodStore periodStore,
      @jakarta.inject.Named("indexExtraction") ExecutorService indexExtractionExecutor) {
    return new IndexWorker(
        chessClient,
        featureExtractor,
        requestStore,
        gameFeatureStore,
        periodStore,
        indexExtractionExecutor);
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

  @Context
  public DataAvailabilityResolver dataAvailabilityResolver(IndexedPeriodStore periodStore) {
    return new DataAvailabilityResolver(periodStore);
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
  public IndexerFacade indexerFacade(
      IndexRequestService indexRequestService,
      GameFeatureStore gameFeatureStore,
      FeatureExtractor featureExtractor,
      SqlCompiler sqlCompiler) {
    return new IndexerFacade(indexRequestService, gameFeatureStore, featureExtractor, sqlCompiler);
  }

  // The tools themselves are @Singleton beans in the tools package, discovered by micronaut-mcp
  // through the @Executable meta-annotation on @Tool. This factory only supplies what they
  // inject: the ChessClient, the IndexerFacade, the Clock, and the named lookup pool.
}
