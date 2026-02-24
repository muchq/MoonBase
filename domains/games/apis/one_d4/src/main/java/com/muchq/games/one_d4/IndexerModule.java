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
import com.muchq.games.one_d4.motifs.CheckDetector;
import com.muchq.games.one_d4.motifs.CheckmateDetector;
import com.muchq.games.one_d4.motifs.CrossPinDetector;
import com.muchq.games.one_d4.motifs.DiscoveredAttackDetector;
import com.muchq.games.one_d4.motifs.DiscoveredCheckDetector;
import com.muchq.games.one_d4.motifs.ForkDetector;
import com.muchq.games.one_d4.motifs.MotifDetector;
import com.muchq.games.one_d4.motifs.PinDetector;
import com.muchq.games.one_d4.motifs.PromotionDetector;
import com.muchq.games.one_d4.motifs.PromotionWithCheckDetector;
import com.muchq.games.one_d4.motifs.PromotionWithCheckmateDetector;
import com.muchq.games.one_d4.motifs.SkewerDetector;
import com.muchq.games.one_d4.queue.InMemoryIndexQueue;
import com.muchq.games.one_d4.queue.IndexQueue;
import com.muchq.games.one_d4.worker.IndexWorker;
import com.muchq.games.one_d4.worker.IndexWorkerLifecycle;
import com.muchq.games.one_d4.worker.RetentionWorker;
import com.muchq.platform.http_client.core.HttpClient;
import com.muchq.platform.http_client.jdk.Jdk11HttpClient;
import com.muchq.platform.json.JsonUtils;
import io.micronaut.context.annotation.Context;
import io.micronaut.context.annotation.Factory;
import io.micronaut.context.annotation.Value;
import java.util.List;
import javax.sql.DataSource;

@Factory
public class IndexerModule {

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
  public DataSource dataSource(
      @Value("${indexer.db.url:jdbc:h2:mem:indexer;DB_CLOSE_DELAY=-1}") String jdbcUrl,
      @Value("${indexer.db.username:sa}") String username,
      @Value("${indexer.db.password:}") String password) {
    return DataSourceFactory.create(jdbcUrl, username, password);
  }

  @Context
  public Migration migration(
      DataSource dataSource,
      @Value("${indexer.db.url:jdbc:h2:mem:indexer;DB_CLOSE_DELAY=-1}") String jdbcUrl) {
    boolean useH2 = jdbcUrl.contains(":h2:");
    Migration migration = new Migration(dataSource, useH2);
    migration.run();
    return migration;
  }

  @Context
  public IndexingRequestStore indexingRequestStore(DataSource dataSource) {
    return new IndexingRequestDao(dataSource);
  }

  @Context
  public GameFeatureStore gameFeatureStore(
      DataSource dataSource,
      @Value("${indexer.db.url:jdbc:h2:mem:indexer;DB_CLOSE_DELAY=-1}") String jdbcUrl) {
    boolean useH2 = jdbcUrl.contains(":h2:");
    return new GameFeatureDao(dataSource, useH2);
  }

  @Context
  public IndexedPeriodStore indexedPeriodStore(
      DataSource dataSource,
      @Value("${indexer.db.url:jdbc:h2:mem:indexer;DB_CLOSE_DELAY=-1}") String jdbcUrl) {
    boolean useH2 = jdbcUrl.contains(":h2:");
    return new IndexedPeriodDao(dataSource, useH2);
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
        new ForkDetector(),
        new SkewerDetector(),
        new DiscoveredAttackDetector(),
        new DiscoveredCheckDetector(),
        new CheckDetector(),
        new CheckmateDetector(),
        new PromotionDetector(),
        new PromotionWithCheckDetector(),
        new PromotionWithCheckmateDetector());
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

  @Context
  public RetentionWorker retentionWorker(
      GameFeatureStore gameFeatureStore, IndexedPeriodStore indexedPeriodStore) {
    return new RetentionWorker(gameFeatureStore, indexedPeriodStore);
  }
}
