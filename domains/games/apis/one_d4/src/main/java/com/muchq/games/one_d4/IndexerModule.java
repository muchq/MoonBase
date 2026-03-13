package com.muchq.games.one_d4;

import com.fasterxml.jackson.databind.ObjectMapper;
import com.muchq.games.chess_com_client.ChessClient;
import com.muchq.games.chessql.compiler.CompiledQuery;
import com.muchq.games.chessql.compiler.QueryCompiler;
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
import io.micronaut.context.annotation.Context;
import io.micronaut.context.annotation.Factory;
import java.io.IOException;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.List;
import javax.sql.DataSource;
import org.jdbi.v3.core.Jdbi;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

@Factory
public class IndexerModule {
  private static final Logger LOG = LoggerFactory.getLogger(IndexerModule.class);
  private static final String DEFAULT_JDBC_URL = "jdbc:h2:mem:indexer;DB_CLOSE_DELAY=-1";
  private static final Path DB_CONFIG_PATH = Path.of("/etc/one_d4/db_config");

  /**
   * Resolves the JDBC URL. Priority: 1. INDEXER_DB_URL environment variable 2.
   * /etc/one_d4/db_config file (plain text, single line) 3. H2 in-memory (local dev default)
   */
  static String readJdbcUrl() {
    String envUrl = System.getenv("INDEXER_DB_URL");
    if (envUrl != null && !envUrl.isBlank()) {
      return envUrl.strip();
    }
    try {
      String fileUrl = Files.readString(DB_CONFIG_PATH).strip();
      if (!fileUrl.isEmpty()) {
        LOG.info("Loaded JDBC URL from {}", DB_CONFIG_PATH);
        return fileUrl;
      }
    } catch (IOException e) {
      throw new UncheckedIOException(ioe);
    }
    LOG.info("No DB config found; falling back to H2 in-memory");
    return DEFAULT_JDBC_URL;
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

  @Context
  public DataSource dataSource() {
    return DataSourceFactory.create(readJdbcUrl());
  }

  @Context
  public Boolean useH2() {
    return readJdbcUrl().contains(":h2:");
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
  public IndexingRequestStore indexingRequestStore(Jdbi jdbi) {
    return new IndexingRequestDao(jdbi);
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
  public QueryCompiler<CompiledQuery> queryCompiler() {
    return new SqlCompiler();
  }

  @Context
  public List<MotifDetector> motifDetectors() {
    return List.of(
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

  @Context
  public IndexWorker indexWorker(
      ChessClient chessClient,
      FeatureExtractor featureExtractor,
      IndexingRequestStore requestStore,
      GameFeatureStore gameFeatureStore,
      IndexedPeriodStore periodStore) {
    return new IndexWorker(
        chessClient, featureExtractor, requestStore, gameFeatureStore, periodStore);
  }

  @Context
  public IndexWorkerLifecycle indexWorkerLifecycle(IndexQueue queue, IndexWorker worker) {
    return new IndexWorkerLifecycle(queue, worker);
  }
}
