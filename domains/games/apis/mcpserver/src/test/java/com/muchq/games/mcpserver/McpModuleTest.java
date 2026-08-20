package com.muchq.games.mcpserver;

import static org.assertj.core.api.Assertions.assertThat;

import com.muchq.games.mcpserver.tools.AggregateGamesTool;
import com.muchq.games.mcpserver.tools.AnalyzePositionTool;
import com.muchq.games.mcpserver.tools.ChessComGamesTool;
import com.muchq.games.mcpserver.tools.ChessComPlayerTool;
import com.muchq.games.mcpserver.tools.ChessComPlayersTool;
import com.muchq.games.mcpserver.tools.ChessComStatsTool;
import com.muchq.games.mcpserver.tools.IndexGamesTool;
import com.muchq.games.mcpserver.tools.IndexStatusTool;
import com.muchq.games.mcpserver.tools.IndexerFacade;
import com.muchq.games.mcpserver.tools.OneD4Client;
import com.muchq.games.mcpserver.tools.QueryGamesTool;
import com.muchq.games.mcpserver.tools.ServerTimeTool;
import io.micronaut.context.ApplicationContext;
import org.junit.jupiter.api.Test;

/**
 * Boots the real Micronaut context so McpModule's eager ({@code @Context}) wiring is exercised in
 * CI rather than only by manual smoke tests.
 */
public class McpModuleTest {

  /**
   * No indexer beans, which is the structural half of #1332. This process reaches the corpus over
   * HTTP; a DataSource or an IndexWorkerLifecycle appearing in this context again would mean
   * someone had re-wired a second indexer, and the deployment would quietly grow a private
   * in-memory database whose contents never reach the site.
   */
  @Test
  public void thereIsNoIndexerWiredIntoThisProcess() {
    try (ApplicationContext context = ApplicationContext.run()) {
      assertThat(context.containsBean(javax.sql.DataSource.class))
          .as("mcpserver must hold no database connection of its own")
          .isFalse();
      assertThat(context.findBean(OneD4Client.class))
          .as("the corpus is reached through this and nothing else")
          .isPresent();
    }
  }

  /**
   * Every tool is a bean the context can build. What the protocol actually advertises is asserted
   * over HTTP in {@link McpToolRosterContractTest}; this is the narrower claim underneath it — that
   * each tool's collaborators resolve — so a broken constructor is distinguishable from a broken
   * registration.
   */
  @Test
  public void everyToolIsAConstructibleBean() {
    try (ApplicationContext context = ApplicationContext.run()) {
      assertThat(context.getBean(ChessComGamesTool.class)).isNotNull();
      assertThat(context.getBean(ChessComPlayerTool.class)).isNotNull();
      assertThat(context.getBean(ChessComPlayersTool.class)).isNotNull();
      assertThat(context.getBean(ChessComStatsTool.class)).isNotNull();
      assertThat(context.getBean(ServerTimeTool.class)).isNotNull();
      assertThat(context.getBean(IndexGamesTool.class)).isNotNull();
      assertThat(context.getBean(IndexStatusTool.class)).isNotNull();
      assertThat(context.getBean(QueryGamesTool.class)).isNotNull();
      assertThat(context.getBean(AggregateGamesTool.class)).isNotNull();
      assertThat(context.getBean(AnalyzePositionTool.class)).isNotNull();
    }
  }

  /**
   * The property override actually reaches the client.
   *
   * <p>Added because it was not: {@code one.d4.base.url} exists so an in-process test can aim
   * mcpserver at an embedded one_d4 on a port chosen at boot, but nothing set it, so a bean that
   * ignored the property and always read the environment would have passed the whole suite.
   */
  @Test
  public void theUpstreamUrlCanBeSetByProperty() {
    try (ApplicationContext context =
        ApplicationContext.run(
            java.util.Map.of("one.d4.base.url", "http://one-d4-under-test:9999"))) {
      assertThat(context.getBean(OneD4Client.class).baseUrl())
          .isEqualTo("http://one-d4-under-test:9999");
    }
  }

  @Test
  public void theV2UpstreamUrlCanBeSetByProperty() {
    try (ApplicationContext context =
        ApplicationContext.run(
            java.util.Map.of("one.d4.v2.base.url", "http://one-d4-v2-under-test:9999"))) {
      assertThat(context.getBean(OneD4Client.class).analyzeBaseUrl())
          .isEqualTo("http://one-d4-v2-under-test:9999");
    }
  }

  /**
   * The facade the tools inject points at the configured upstream. A default that silently pointed
   * somewhere else would leave every corpus tool answering "not reachable" in production while
   * every unit test stayed green.
   */
  @Test
  public void theFacadePointsAtTheConfiguredUpstream() {
    try (ApplicationContext context = ApplicationContext.run()) {
      assertThat(context.getBean(IndexerFacade.class)).isNotNull();
      assertThat(context.getBean(OneD4Client.class).baseUrl()).isEqualTo(McpModule.oneD4BaseUrl());
      assertThat(context.getBean(OneD4Client.class).analyzeBaseUrl())
          .isEqualTo(McpModule.oneD4V2BaseUrl());
    }
  }
}
