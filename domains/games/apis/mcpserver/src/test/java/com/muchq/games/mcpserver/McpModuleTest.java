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
import com.muchq.games.mcpserver.tools.QueryGamesTool;
import com.muchq.games.mcpserver.tools.ServerTimeTool;
import com.muchq.games.one_d4.worker.IndexWorkerLifecycle;
import io.micronaut.context.ApplicationContext;
import org.junit.jupiter.api.Test;

/**
 * Boots the real Micronaut context so McpModule's eager (@Context) wiring — including the
 * in-process indexer's DataSource, migration, worker, and facade — is exercised in CI rather than
 * only by manual smoke tests.
 */
public class McpModuleTest {

  /**
   * The container's half of the shutdown contract, exercised for real: boot, then close, and the
   * poller must have been told to stop.
   *
   * <p>Worth a real context rather than reading the annotation back off the factory method. The
   * poller is a daemon thread, so if Micronaut does not invoke preDestroy — wrong bean scope, a
   * misspelled method name, a refactor that renames stop() — nothing anywhere fails, and every
   * deploy silently strands its in-flight request for a full lease and spends one of its three
   * attempts. This is the one place that catches that.
   */
  @Test
  public void closingTheContextStopsTheIndexWorker() {
    IndexWorkerLifecycle lifecycle;
    try (ApplicationContext context = ApplicationContext.run()) {
      lifecycle = context.getBean(IndexWorkerLifecycle.class);
      assertThat(lifecycle.isRunning()).as("the poller should be live while the app is").isTrue();
    }

    assertThat(lifecycle.isRunning())
        .as("shutdown must reach the poller, or every deploy looks like a crash")
        .isFalse();
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

  @Test
  public void toolsRunAgainstTheWiredIndexer() {
    try (ApplicationContext context = ApplicationContext.run()) {
      // Query against the freshly migrated in-memory H2 — proves parser, compiler, and DB wiring
      String result =
          context
              .getBean(QueryGamesTool.class)
              .queryChessGames("white.elo >= 2500", null, null, null);
      assertThat(result).contains("\"games\":[]").contains("\"count\":0");
    }
  }
}
