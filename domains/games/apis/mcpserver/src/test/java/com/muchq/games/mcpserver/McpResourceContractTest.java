package com.muchq.games.mcpserver;

import static org.assertj.core.api.Assertions.assertThat;

import com.fasterxml.jackson.databind.JsonNode;
import com.muchq.platform.json.JsonUtils;
import io.micronaut.context.ApplicationContext;
import io.micronaut.inject.BeanDefinition;
import io.micronaut.inject.ExecutableMethod;
import io.micronaut.mcp.annotations.Resource;
import io.micronaut.runtime.server.EmbeddedServer;
import io.modelcontextprotocol.spec.McpSchema.ReadResourceResult;
import java.io.InputStream;
import java.net.URI;
import java.net.http.HttpClient;
import java.net.http.HttpRequest;
import java.net.http.HttpResponse;
import java.nio.charset.StandardCharsets;
import java.util.ArrayList;
import java.util.List;
import java.util.Map;
import org.junit.jupiter.api.AfterAll;
import org.junit.jupiter.api.BeforeAll;
import org.junit.jupiter.api.Test;

/**
 * The ChessQL reference is only useful if a client can actually reach it, so this asks the running
 * server the two questions a client asks — {@code resources/list} then {@code resources/read} —
 * rather than calling the bean.
 *
 * <p>The rosters inside the served markdown are pinned separately, by {@code ChessQlReferenceTest}
 * in one_d4, against {@code SqlCompiler}. Splitting them that way keeps each contract next to the
 * thing it constrains: that the doc agrees with the compiler is a one_d4 concern and true whether
 * or not MCP exists, while whether the bytes reach a client is this server's. What joins the two
 * halves is the assertion below that the served text is the doc byte-for-byte — so the roster
 * pinned there is the roster delivered here.
 */
public class McpResourceContractTest {

  private static final String REFERENCE_URI = "chessql://reference";

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
  public void theServerAdvertisesTheChessQlReference() throws Exception {
    JsonNode resources = rpc("resources/list", "{}").at("/result/resources");

    List<String> uris = new ArrayList<>();
    resources.forEach(resource -> uris.add(resource.get("uri").asText()));
    assertThat(uris).contains(REFERENCE_URI);

    JsonNode reference = null;
    for (JsonNode resource : resources) {
      if (REFERENCE_URI.equals(resource.get("uri").asText())) {
        reference = resource;
      }
    }
    assertThat(reference).isNotNull();
    // text/markdown, not the text/plain micronaut-mcp falls back to when mimeType is unset: a
    // client that renders by mime type shows a wall of pipes and backticks otherwise.
    assertThat(reference.get("mimeType").asText()).isEqualTo("text/markdown");
    assertThat(reference.get("name").asText()).isEqualTo("chessql_reference");
  }

  @Test
  public void readingTheReferenceReturnsTheDocumentVerbatim() throws Exception {
    JsonNode contents =
        rpc("resources/read", "{\"uri\":\"" + REFERENCE_URI + "\"}").at("/result/contents");

    assertThat(contents).hasSize(1);
    JsonNode content = contents.get(0);
    assertThat(content.get("uri").asText()).isEqualTo(REFERENCE_URI);
    assertThat(content.get("mimeType").asText()).isEqualTo("text/markdown");
    assertThat(content.get("text").asText()).isEqualTo(packagedReference());
  }

  /**
   * Not vacuous-content insurance: the equality above would hold just as well if the doc were
   * empty, and an empty reference is the exact failure this resource exists to prevent. These two
   * headings are what a tool description cannot carry, so their absence would mean the resource is
   * not earning its keep even while the wiring "works".
   *
   * <p>Headings only, deliberately. An earlier draft also pinned the sentence stating the
   * precedence rule, which made rewording one line of prose in <em>one_d4's</em> CHESSQL.md break a
   * test in mcpserver — the cross-module coupling {@code McpToolRosterContractTest} explicitly
   * refuses for tool descriptions. Structure here; the rosters are pinned in one_d4, where the doc
   * lives.
   */
  @Test
  public void theServedReferenceCarriesWhatTheToolDescriptionCannot() throws Exception {
    String text =
        rpc("resources/read", "{\"uri\":\"" + REFERENCE_URI + "\"}")
            .at("/result/contents/0/text")
            .asText();

    assertThat(text).contains("## Grammar (EBNF)").contains("## Operator Precedence");
  }

  /**
   * micronaut-mcp maps a {@code String} return to {@code TextResourceContents} and a {@code
   * ReadResourceResult} straight through — and maps anything else to a result with no contents at
   * all, no error raised. A resource whose method returned, say, {@code byte[]} would list
   * correctly and read empty, which is indistinguishable from documentation that says nothing.
   *
   * <p>Same shape, and the same reasoning, as {@code McpToolRosterContractTest}'s check that every
   * tool returns {@code CallToolResult}: the first wrong return type is the template for the next
   * one.
   */
  @Test
  public void everyResourceReturnsATypeTheProtocolCanCarry() {
    List<String> offenders = new ArrayList<>();
    int resources = 0;

    for (BeanDefinition<?> definition : server.getApplicationContext().getAllBeanDefinitions()) {
      for (ExecutableMethod<?, ?> method : definition.getExecutableMethods()) {
        if (!method.hasAnnotation(Resource.class)) {
          continue;
        }
        resources++;
        Class<?> returnType = method.getReturnType().getType();
        if (!String.class.equals(returnType) && !ReadResourceResult.class.equals(returnType)) {
          offenders.add(
              definition.getBeanType().getSimpleName()
                  + "."
                  + method.getMethodName()
                  + " returns "
                  + returnType.getSimpleName());
        }
      }
    }

    assertThat(resources)
        .as("no @Resource methods were found, so this test would pass against anything")
        .isPositive();
    assertThat(offenders)
        .as("micronaut-mcp reads any other return type as empty contents, with no error")
        .isEmpty();
  }

  private static JsonNode rpc(String method, String params) throws Exception {
    HttpRequest request =
        HttpRequest.newBuilder()
            .uri(URI.create("http://localhost:" + server.getPort() + "/mcp"))
            .header("Content-Type", "application/json")
            .POST(
                HttpRequest.BodyPublishers.ofString(
                    "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\""
                        + method
                        + "\",\"params\":"
                        + params
                        + "}"))
            .build();
    HttpResponse<String> response =
        HttpClient.newHttpClient().send(request, HttpResponse.BodyHandlers.ofString());
    assertThat(response.statusCode()).as("%s HTTP status", method).isEqualTo(200);

    JsonNode body = JsonUtils.mapper().readTree(response.body());
    assertThat(body.has("error")).as("%s returned %s", method, response.body()).isFalse();
    return body;
  }

  /**
   * Read through the production {@code :resources} library, deliberately — this target does not
   * depend on {@code :chessql_reference} itself. Giving the test its own copy of that dep is what
   * the first draft did, and mutation testing caught it: dropping the doc from the library that
   * ships inside the binary left this test green, because the test was reading its own copy while
   * the deployed server would have thrown on first read. Now the only path to these bytes is the
   * one the binary uses, so a packaging mistake fails here.
   */
  private static String packagedReference() throws Exception {
    try (InputStream in = McpResourceContractTest.class.getResourceAsStream("/CHESSQL.md")) {
      assertThat(in)
          .as("CHESSQL.md must reach the classpath through mcpserver's :resources library")
          .isNotNull();
      return new String(in.readAllBytes(), StandardCharsets.UTF_8);
    }
  }
}
