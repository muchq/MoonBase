package com.muchq.games.mcpserver.tools;

import static org.assertj.core.api.Assertions.assertThat;

import com.fasterxml.jackson.databind.JsonNode;
import com.muchq.games.mcpserver.McpModule;
import com.muchq.platform.json.JsonUtils;
import java.io.InputStream;
import java.time.Clock;
import java.util.ArrayList;
import java.util.List;
import org.junit.jupiter.api.Test;

/**
 * The tool roster is a published contract, not an internal detail: 1d4.net's {@code /mcp} page
 * documents it, and that page cannot fetch {@code tools/list} at runtime — {@code mcp.1d4.net}
 * sends no CORS headers, so a browser call from 1d4.net is blocked. A hand-maintained table on the
 * site would therefore rot silently the first time a tool is added or renamed.
 *
 * <p>{@code mcp_tools.json} closes that loop from both ends. This test fails when the registry
 * stops matching the file, and {@code McpView.test.tsx} in 1d4_web fails when the page's table
 * stops matching the same file — so a tool change breaks the build here first, and again on the
 * frontend until the page is updated.
 *
 * <p>Names only, deliberately. Descriptions and input schemas are prose the tools own and reword
 * freely; pinning them here would turn every wording improvement into a two-repo-directory edit for
 * no reader's benefit. What the page promises, and what a change to it must be deliberate about, is
 * which tools exist.
 */
public class McpToolRegistryContractTest {

  @Test
  public void theRegistryAdvertisesExactlyTheToolsTheSiteDocuments() throws Exception {
    List<String> documented = documentedToolNames();
    List<String> registered =
        new ToolRegistry(allTools()).getTools().stream().map(t -> t.name()).sorted().toList();

    assertThat(registered)
        .as(
            "mcp_tools.json is the contract between this registry and 1d4.net's /mcp page;"
                + " update it (and the page's table) alongside the tool")
        .isEqualTo(documented);
  }

  private static List<String> documentedToolNames() throws Exception {
    try (InputStream in =
        McpToolRegistryContractTest.class.getClassLoader().getResourceAsStream("mcp_tools.json")) {
      assertThat(in).as("mcp_tools.json must be on the test classpath").isNotNull();
      JsonNode root = JsonUtils.mapper().readTree(in);
      List<String> names = new ArrayList<>();
      root.get("tools").forEach(n -> names.add(n.asText()));
      return names.stream().sorted().toList();
    }
  }

  /**
   * The server's own tool list, from the factory the running server uses — not a copy of it. A
   * second hand-written list here would be one more place to forget, which is the failure this
   * whole file exists to prevent.
   *
   * <p>Collaborators are null because the factory only hands them to constructors that store them;
   * nothing is called. A tool that reached into a collaborator while being constructed would fail
   * here, and should — registry construction runs at startup, before anything is ready.
   */
  private static List<McpTool> allTools() {
    return new McpModule().mcpTools(Clock.systemUTC(), null, null, null);
  }
}
