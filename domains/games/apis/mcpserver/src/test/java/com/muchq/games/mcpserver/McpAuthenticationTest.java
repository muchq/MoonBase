package com.muchq.games.mcpserver;

import static org.assertj.core.api.Assertions.assertThat;

import io.micronaut.context.ApplicationContext;
import io.micronaut.runtime.server.EmbeddedServer;
import java.net.URI;
import java.net.http.HttpClient;
import java.net.http.HttpRequest;
import java.net.http.HttpResponse;
import java.util.Map;
import org.junit.jupiter.api.Test;

/**
 * {@link McpAuthenticationFilter} has two states and they are easy to confuse: the shared {@code
 * application.yml} always defines {@code mcp.auth.token}, so what decides is whether it holds
 * anything, not whether it is set. Empty leaves the endpoint open, and empty is what the deployment
 * runs. Both halves are pinned here so "mcp.1d4.net is unauthenticated" reads as a decision someone
 * made rather than a wiring accident (#1325).
 *
 * <p>The filter is also pinned to the endpoint it is supposed to guard. Both it and the controller
 * read {@code micronaut.mcp.server.endpoint}, and the last test here moves that endpoint to prove
 * the gate moves with it — a filter pattern left behind on the old path is a token that guards
 * nothing while still looking configured.
 */
public class McpAuthenticationTest {

  private static final String TOKEN = "s3cret-token";

  private static final String TOOLS_LIST =
      "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"tools/list\"}";

  /**
   * The token is set to empty explicitly rather than left to application.yml's {@code
   * ${MCP_AUTH_TOKEN:}} default: an ambient MCP_AUTH_TOKEN in the environment would otherwise turn
   * this into the gated case and fail somewhere confusing.
   */
  @Test
  public void withNoTokenConfiguredTheEndpointIsOpen() throws Exception {
    try (EmbeddedServer server = startServer(Map.of("mcp.auth.token", ""))) {
      HttpResponse<String> response = post(server, TOOLS_LIST, null);

      assertThat(response.statusCode())
          .as("the deployment sets no MCP_AUTH_TOKEN; this is the behavior mcp.1d4.net has")
          .isEqualTo(200);
      assertThat(response.body()).contains("\"tools\"");
    }
  }

  @Test
  public void withATokenConfiguredAnUnauthenticatedCallIsRejected() throws Exception {
    try (EmbeddedServer server = startServer(Map.of("mcp.auth.token", TOKEN))) {
      HttpResponse<String> response = post(server, TOOLS_LIST, null);

      assertThat(response.statusCode()).isEqualTo(401);
      assertThat(response.body())
          .as("a client that only parses MCP responses still needs something to show")
          .contains("Missing Authorization header");
      assertThat(response.body()).doesNotContain("\"tools\"");
    }
  }

  @Test
  public void withATokenConfiguredTheWrongTokenIsRejected() throws Exception {
    try (EmbeddedServer server = startServer(Map.of("mcp.auth.token", TOKEN))) {
      HttpResponse<String> wrongToken = post(server, TOOLS_LIST, "Bearer not-the-token");
      assertThat(wrongToken.statusCode()).isEqualTo(401);
      assertThat(wrongToken.body()).contains("Invalid authentication token");

      HttpResponse<String> wrongScheme = post(server, TOOLS_LIST, "Basic " + TOKEN);
      assertThat(wrongScheme.statusCode()).isEqualTo(401);
      assertThat(wrongScheme.body()).contains("Invalid Authorization header format");
    }
  }

  @Test
  public void withATokenConfiguredTheRightTokenGetsThrough() throws Exception {
    try (EmbeddedServer server = startServer(Map.of("mcp.auth.token", TOKEN))) {
      HttpResponse<String> response = post(server, TOOLS_LIST, "Bearer " + TOKEN);

      assertThat(response.statusCode()).isEqualTo(200);
      assertThat(response.body()).contains("\"tools\"");
    }
  }

  /** The liveness probe is not behind the token — compose has no way to send one. */
  @Test
  public void theHealthProbeStaysReachableWhileTheTokenIsSet() throws Exception {
    try (EmbeddedServer server = startServer(Map.of("mcp.auth.token", TOKEN))) {
      HttpRequest request =
          HttpRequest.newBuilder().uri(URI.create(baseUrl(server) + "/health")).GET().build();
      HttpResponse<String> response =
          HttpClient.newHttpClient().send(request, HttpResponse.BodyHandlers.ofString());

      assertThat(response.statusCode()).isEqualTo(200);
    }
  }

  /**
   * Move the endpoint and the gate must move with it. Without this, a filter pattern that hardcoded
   * {@code /mcp} would keep passing every test above while leaving a relocated endpoint wide open.
   */
  @Test
  public void theGateFollowsTheConfiguredEndpoint() throws Exception {
    try (EmbeddedServer server =
        startServer(Map.of("mcp.auth.token", TOKEN, "micronaut.mcp.server.endpoint", "/mcp-alt"))) {
      assertThat(postTo(server, "/mcp-alt", null).statusCode())
          .as("the relocated endpoint must be behind the token")
          .isEqualTo(401);
      assertThat(postTo(server, "/mcp-alt", "Bearer " + TOKEN).statusCode()).isEqualTo(200);

      assertThat(postTo(server, "/mcp", "Bearer " + TOKEN).statusCode())
          .as("and nothing should still be answering MCP on the old path")
          .isNotEqualTo(200);
    }
  }

  private static EmbeddedServer startServer(Map<String, Object> properties) {
    Map<String, Object> merged = new java.util.HashMap<>(properties);
    merged.put("micronaut.server.port", "-1");
    return ApplicationContext.run(EmbeddedServer.class, merged);
  }

  private static String baseUrl(EmbeddedServer server) {
    return "http://localhost:" + server.getPort();
  }

  private static HttpResponse<String> post(EmbeddedServer server, String body, String authorization)
      throws Exception {
    return postTo(server, "/mcp", authorization, body);
  }

  private static HttpResponse<String> postTo(
      EmbeddedServer server, String path, String authorization) throws Exception {
    return postTo(server, path, authorization, TOOLS_LIST);
  }

  private static HttpResponse<String> postTo(
      EmbeddedServer server, String path, String authorization, String body) throws Exception {
    HttpRequest.Builder builder =
        HttpRequest.newBuilder()
            .uri(URI.create(baseUrl(server) + path))
            .header("Content-Type", "application/json")
            .POST(HttpRequest.BodyPublishers.ofString(body));
    if (authorization != null) {
      builder.header("Authorization", authorization);
    }
    return HttpClient.newHttpClient().send(builder.build(), HttpResponse.BodyHandlers.ofString());
  }
}
