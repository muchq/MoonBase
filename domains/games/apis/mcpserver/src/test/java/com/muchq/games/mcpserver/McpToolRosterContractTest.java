package com.muchq.games.mcpserver;

import static org.assertj.core.api.Assertions.assertThat;

import com.fasterxml.jackson.databind.JsonNode;
import com.muchq.platform.json.JsonUtils;
import io.micronaut.context.ApplicationContext;
import io.micronaut.runtime.server.EmbeddedServer;
import java.io.InputStream;
import java.net.URI;
import java.net.http.HttpClient;
import java.net.http.HttpRequest;
import java.net.http.HttpResponse;
import java.util.ArrayList;
import java.util.List;
import java.util.Map;
import org.junit.jupiter.api.AfterAll;
import org.junit.jupiter.api.BeforeAll;
import org.junit.jupiter.api.Test;

/**
 * The tool roster is a published contract, not an internal detail: 1d4.net's {@code /mcp} page
 * documents it, and {@code mcp_tools.json} closes that loop from both ends. This test fails when
 * the server stops advertising exactly that list, and {@code McpView.test.tsx} in 1d4_web fails
 * when the page's table stops matching the same file — so a tool change breaks the build here
 * first, and again on the frontend until the page is updated.
 *
 * <p>The roster stays checked in rather than fetched by the page at runtime, even though CORS
 * allows the call. A build-time contract fails in CI, before a deploy; a runtime fetch can only
 * ever be wrong in production, and would put an empty table on the page whenever the MCP server is
 * down.
 *
 * <p>It asks the running server over {@code tools/list} — the same answer a client gets, derived
 * from the {@code @Tool} methods themselves. Nothing between the annotations and this assertion is
 * hand-maintained, so a tool that fails to register (a missing {@code @Singleton}, a bean the
 * context cannot construct) fails here rather than going quietly missing from the deployment.
 *
 * <p>Names only, deliberately. Descriptions and input schemas are prose the tools own and reword
 * freely; pinning them here would turn every wording improvement into a two-repo-directory edit for
 * no reader's benefit. What the page promises, and what a change to it must be deliberate about, is
 * which tools exist.
 */
public class McpToolRosterContractTest {

  private static EmbeddedServer server;

  @BeforeAll
  public static void startServer() {
    server = ApplicationContext.run(EmbeddedServer.class, Map.of("micronaut.server.port", "-1"));
  }

  @AfterAll
  public static void stopServer() {
    server.stop();
  }

  @Test
  public void theServerAdvertisesExactlyTheToolsTheSiteDocuments() throws Exception {
    assertThat(advertisedToolNames())
        .as(
            "mcp_tools.json is the contract between this server and 1d4.net's /mcp page;"
                + " update it (and the page's table) alongside the tool")
        .isEqualTo(documentedToolNames());
  }

  private static List<String> advertisedToolNames() throws Exception {
    HttpRequest request =
        HttpRequest.newBuilder()
            .uri(URI.create("http://localhost:" + server.getPort() + "/mcp"))
            .header("Content-Type", "application/json")
            .POST(
                HttpRequest.BodyPublishers.ofString(
                    "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"tools/list\"}"))
            .build();
    HttpResponse<String> response =
        HttpClient.newHttpClient().send(request, HttpResponse.BodyHandlers.ofString());
    assertThat(response.statusCode()).isEqualTo(200);

    List<String> names = new ArrayList<>();
    JsonUtils.mapper()
        .readTree(response.body())
        .at("/result/tools")
        .forEach(tool -> names.add(tool.get("name").asText()));
    return names.stream().sorted().toList();
  }

  private static List<String> documentedToolNames() throws Exception {
    try (InputStream in =
        McpToolRosterContractTest.class.getClassLoader().getResourceAsStream("mcp_tools.json")) {
      assertThat(in).as("mcp_tools.json must be on the test classpath").isNotNull();
      JsonNode root = JsonUtils.mapper().readTree(in);
      List<String> names = new ArrayList<>();
      root.get("tools").forEach(n -> names.add(n.asText()));
      return names.stream().sorted().toList();
    }
  }
}
