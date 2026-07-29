package com.muchq.games.mcpserver;

import com.fasterxml.jackson.databind.ObjectMapper;
import com.muchq.games.chess_com_client.ChessClient;
import com.muchq.games.chessql.compiler.SqlCompiler;
import com.muchq.games.mcpserver.tools.AggregateGamesTool;
import com.muchq.games.mcpserver.tools.AnalyzePositionTool;
import com.muchq.games.mcpserver.tools.ChessComGamesTool;
import com.muchq.games.mcpserver.tools.ChessComPlayerTool;
import com.muchq.games.mcpserver.tools.ChessComPlayersTool;
import com.muchq.games.mcpserver.tools.ChessComStatsTool;
import com.muchq.games.mcpserver.tools.IndexGamesTool;
import com.muchq.games.mcpserver.tools.IndexStatusTool;
import com.muchq.games.mcpserver.tools.IndexerFacade;
import com.muchq.games.mcpserver.tools.McpTool;
import com.muchq.games.mcpserver.tools.QueryGamesTool;
import com.muchq.games.mcpserver.tools.ServerTimeTool;
import com.muchq.games.mcpserver.tools.ToolRegistry;
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
import com.muchq.games.one_d4.motifs.AttackDetector;
import com.muchq.games.one_d4.motifs.BackRankMateDetector;
import com.muchq.games.one_d4.motifs.CheckDetector;
import com.muchq.games.one_d4.motifs.CrossPinDetector;
import com.muchq.games.one_d4.motifs.MotifDetector;
import com.muchq.games.one_d4.motifs.PinDetector;
import com.muchq.games.one_d4.motifs.PromotionDetector;
import com.muchq.games.one_d4.motifs.PromotionWithCheckDetector;
import com.muchq.games.one_d4.motifs.PromotionWithCheckmateDetector;
import com.muchq.games.one_d4.motifs.SkewerDetector;
import com.muchq.games.one_d4.motifs.SmotheredMateDetector;
import com.muchq.games.one_d4.queue.InMemoryIndexQueue;
import com.muchq.games.one_d4.queue.IndexQueue;
import com.muchq.games.one_d4.worker.IndexWorker;
import com.muchq.games.one_d4.worker.IndexWorkerLifecycle;
import com.muchq.platform.http_client.core.HttpClient;
import com.muchq.platform.http_client.jdk.Jdk11HttpClient;
import com.muchq.platform.json.JsonUtils;
import io.micronaut.context.annotation.Bean;
import io.micronaut.context.annotation.Context;
import io.micronaut.context.annotation.Factory;
import java.time.Clock;
import java.util.List;
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
  public ChessClient chessClient(HttpClient httpClient, ObjectMapper objectMapper) {
    return new ChessClient(httpClient, objectMapper);
  }

  @Context
  public ObjectMapper objectMapper() {
    return JsonUtils.mapper();
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
  public IndexingRequestStore indexingRequestStore(Jdbi jdbi) {
    return new IndexingRequestDao(jdbi);
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
    List<MotifDetector> detectors =
        List.of(
            new PinDetector(),
            new CrossPinDetector(),
            new SkewerDetector(),
            new AttackDetector(),
            new CheckDetector(),
            new PromotionDetector(),
            new PromotionWithCheckDetector(),
            new PromotionWithCheckmateDetector(),
            new BackRankMateDetector(),
            new SmotheredMateDetector());
    return new FeatureExtractor(new PgnParser(), new GameReplayer(), detectors);
  }

  private static final AtomicInteger EXTRACT_THREAD_COUNTER = new AtomicInteger();

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

  @Context
  public IndexWorkerLifecycle indexWorkerLifecycle(IndexQueue queue, IndexWorker worker) {
    return new IndexWorkerLifecycle(queue, worker);
  }

  @Context
  public IndexerFacade indexerFacade(
      IndexingRequestStore requestStore,
      GameFeatureStore gameFeatureStore,
      IndexQueue queue,
      IndexWorker worker,
      FeatureExtractor featureExtractor,
      SqlCompiler sqlCompiler) {
    return new IndexerFacade(
        requestStore, gameFeatureStore, queue, worker, featureExtractor, sqlCompiler);
  }

  @Context
  public List<McpTool> mcpTools(
      Clock clock,
      ChessClient chessClient,
      ObjectMapper objectMapper,
      IndexerFacade indexerFacade) {
    return List.of(
        new ChessComGamesTool(chessClient, objectMapper),
        new ChessComPlayerTool(chessClient, objectMapper),
        new ChessComPlayersTool(chessClient, objectMapper),
        new ChessComStatsTool(chessClient, objectMapper),
        new ServerTimeTool(clock),
        new IndexGamesTool(indexerFacade, objectMapper),
        new IndexStatusTool(indexerFacade, objectMapper),
        new QueryGamesTool(indexerFacade, objectMapper),
        new AggregateGamesTool(indexerFacade, objectMapper),
        new AnalyzePositionTool(indexerFacade, objectMapper));
  }

  @Context
  public ToolRegistry toolRegistry(List<McpTool> tools) {
    return new ToolRegistry(tools);
  }
}
