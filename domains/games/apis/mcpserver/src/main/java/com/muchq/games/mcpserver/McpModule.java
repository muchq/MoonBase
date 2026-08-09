package com.muchq.games.mcpserver;

import com.fasterxml.jackson.databind.ObjectMapper;
import com.muchq.games.chess_com_client.ChessClient;
import com.muchq.games.mcpserver.tools.IndexerFacade;
import com.muchq.games.mcpserver.tools.OneD4Client;
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

@Factory
public class McpModule {

  /**
   * Where the corpus lives. The Compose network name, because this is a service-to-service call on
   * the internal network — one_d4's API is not routed publicly for these paths and does not need to
   * be (#1332).
   */
  private static final String DEFAULT_ONE_D4_URL = "http://one_d4:8080";

  // Deliberately not the injected Micronaut ObjectMapper: chess.com responses carry fields this
  // client does not model, and JsonUtils' mapper is the one configured to ignore them. Tool
  // payloads get the same treatment for the same reason — see ToolJson. one_d4's responses go
  // through it too, so a field added upstream does not break this client on deploy.
  private static final ObjectMapper CLIENT_MAPPER = JsonUtils.mapper();

  static String oneD4BaseUrl() {
    String env = System.getenv("ONE_D4_BASE_URL");
    return env != null && !env.isBlank() ? env.strip() : DEFAULT_ONE_D4_URL;
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
    return new ChessClient(httpClient, CLIENT_MAPPER);
  }

  @Context
  public OneD4Client oneD4Client(HttpClient httpClient) {
    return new OneD4Client(httpClient, CLIENT_MAPPER, oneD4BaseUrl());
  }

  @Context
  public IndexerFacade indexerFacade(OneD4Client oneD4Client) {
    return new IndexerFacade(oneD4Client);
  }

  private static final AtomicInteger LOOKUP_THREAD_COUNTER = new AtomicInteger();

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

  // The tools themselves are @Singleton beans in the tools package, discovered by micronaut-mcp
  // through the @Executable meta-annotation on @Tool. This factory only supplies what they
  // inject: the ChessClient, the IndexerFacade, the Clock, and the named lookup pool.
  //
  // What is no longer here is the point of #1332: no DataSource, no Jdbi, no Migration, no
  // GameFeatureStore, no IndexQueue, no IndexWorker, no FeatureExtractor and no SqlCompiler. This
  // process used to run a whole second indexer against its own database — in the deployment an
  // in-memory one, so a client's indexing never reached the corpus the site serves.
}
