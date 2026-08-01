package com.muchq.games.mcpserver;

import static org.assertj.core.api.Assertions.assertThat;

import com.muchq.games.mcpserver.dtos.Tool;
import com.muchq.games.mcpserver.tools.ToolRegistry;
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

  @Test
  public void contextStartsAndRegistersAllTools() {
    try (ApplicationContext context = ApplicationContext.run()) {
      ToolRegistry registry = context.getBean(ToolRegistry.class);

      assertThat(registry.getTools())
          .extracting(Tool::name)
          .containsExactlyInAnyOrder(
              "chess_com_games",
              "chess_com_player",
              "chess_com_players",
              "chess_com_stats",
              "server_time",
              "index_chess_games",
              "index_status",
              "query_chess_games",
              "aggregate_chess_games",
              "analyze_position");
    }
  }

  @Test
  public void toolsAreExecutableThroughTheRegistry() {
    try (ApplicationContext context = ApplicationContext.run()) {
      ToolRegistry registry = context.getBean(ToolRegistry.class);

      // Query against the freshly migrated in-memory H2 — proves parser, compiler, and DB wiring
      String result =
          registry.executeTool("query_chess_games", java.util.Map.of("query", "white.elo >= 2500"));
      assertThat(result).contains("\"games\":[]").contains("\"count\":0");
    }
  }
}
