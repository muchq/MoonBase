package com.muchq.indexer;

import com.fasterxml.jackson.databind.ObjectMapper;
import com.muchq.chess_com_api.ChessClient;
import com.muchq.http_client.core.HttpClient;
import com.muchq.http_client.jdk.Jdk11HttpClient;
import com.muchq.indexer.chessql.compiler.SqlCompiler;
import com.muchq.indexer.db.DataSourceFactory;
import com.muchq.indexer.db.GameFeatureDao;
import com.muchq.indexer.db.IndexingRequestDao;
import com.muchq.indexer.db.Migration;
import com.muchq.indexer.engine.FeatureExtractor;
import com.muchq.indexer.engine.GameReplayer;
import com.muchq.indexer.engine.PgnParser;
import com.muchq.indexer.motifs.CrossPinDetector;
import com.muchq.indexer.motifs.DiscoveredAttackDetector;
import com.muchq.indexer.motifs.ForkDetector;
import com.muchq.indexer.motifs.MotifDetector;
import com.muchq.indexer.motifs.PinDetector;
import com.muchq.indexer.motifs.SkewerDetector;
import com.muchq.indexer.queue.InMemoryIndexQueue;
import com.muchq.indexer.queue.IndexQueue;
import com.muchq.indexer.worker.IndexWorker;
import com.muchq.indexer.worker.IndexWorkerLifecycle;
import com.muchq.json.JsonUtils;
import io.micronaut.context.annotation.Context;
import io.micronaut.context.annotation.Factory;
import io.micronaut.context.annotation.Value;

import javax.sql.DataSource;
import java.util.List;

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
            @Value("${indexer.db.url:jdbc:postgresql://localhost:5432/indexer}") String jdbcUrl,
            @Value("${indexer.db.username:indexer}") String username,
            @Value("${indexer.db.password:indexer}") String password) {
        return DataSourceFactory.create(jdbcUrl, username, password);
    }

    @Context
    public Migration migration(DataSource dataSource) {
        Migration migration = new Migration(dataSource);
        migration.run();
        return migration;
    }

    @Context
    public IndexingRequestDao indexingRequestDao(DataSource dataSource) {
        return new IndexingRequestDao(dataSource);
    }

    @Context
    public GameFeatureDao gameFeatureDao(DataSource dataSource) {
        return new GameFeatureDao(dataSource);
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
    public List<MotifDetector> motifDetectors() {
        return List.of(
                new PinDetector(),
                new CrossPinDetector(),
                new ForkDetector(),
                new SkewerDetector(),
                new DiscoveredAttackDetector()
        );
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
    public FeatureExtractor featureExtractor(PgnParser pgnParser, GameReplayer replayer, List<MotifDetector> detectors) {
        return new FeatureExtractor(pgnParser, replayer, detectors);
    }

    @Context
    public IndexWorker indexWorker(
            ChessClient chessClient,
            FeatureExtractor featureExtractor,
            IndexingRequestDao requestDao,
            GameFeatureDao gameFeatureDao,
            ObjectMapper objectMapper) {
        return new IndexWorker(chessClient, featureExtractor, requestDao, gameFeatureDao, objectMapper);
    }

    @Context
    public IndexWorkerLifecycle indexWorkerLifecycle(IndexQueue queue, IndexWorker worker) {
        return new IndexWorkerLifecycle(queue, worker);
    }
}
