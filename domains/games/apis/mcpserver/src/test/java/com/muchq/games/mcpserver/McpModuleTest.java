package com.muchq.games.mcpserver;

import static org.assertj.core.api.Assertions.assertThat;

import com.muchq.games.mcpserver.dtos.Tool;
import com.muchq.games.mcpserver.tools.ToolRegistry;
import io.micronaut.context.ApplicationContext;
import org.junit.jupiter.api.Test;

/**
 * Boots the real Micronaut context so McpModule's eager (@Context) wiring — including the
 * in-process indexer's DataSource, migration, worker, and facade — is exercised in CI rather than
 * only by manual smoke tests.
 */
public class McpModuleTest {

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
