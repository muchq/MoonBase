package com.muchq.games.mcpserver;

import static org.assertj.core.api.Assertions.assertThat;

import com.fasterxml.jackson.databind.JsonNode;
import com.muchq.games.chessql.compiler.SqlCompiler;
import com.muchq.platform.json.JsonUtils;
import io.micronaut.context.ApplicationContext;
import io.micronaut.runtime.server.EmbeddedServer;
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
 * The ChessQL vocabulary appears in three places: {@link SqlCompiler}, CHESSQL.md, and {@code
 * query_chess_games}' description. #1326 added the resource and pinned the doc; this pins the
 * description, which is the copy that had actually drifted — twice.
 *
 * <p>It listed 14 of 16 motifs (missing {@code zugzwang} and {@code overloaded_piece}) and 15 of 17
 * fields (missing {@code game.url} and {@code played.at}). Both were hand-patched, and
 * hand-patching without a pin is exactly how they got wrong in the first place — so this exists to
 * make the next addition fail here instead of shipping an incomplete roster.
 *
 * <p>The description matters independently of the resource. Clients are not required to read
 * resources, and 1d4.net's /mcp page tells readers that the tool descriptions are the fallback path
 * for exactly that reason; a model on such a client sees only this text.
 *
 * <p>Deliberately token containment, not equality with the rendered sentence. {@code
 * McpToolRosterContractTest} refuses to pin descriptions because they are prose the tools reword
 * freely, and that still holds: this asserts every name is *present somewhere* in the description
 * and says nothing about order, phrasing, or the sentences around them.
 */
public class McpToolVocabularyTest {

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
  public void theQueryToolDescriptionNamesEveryFieldTheCompilerAccepts() throws Exception {
    String description = descriptionOf("query_chess_games");

    List<String> missing = new ArrayList<>();
    for (String field : SqlCompiler.filterableFields()) {
      if (!description.contains(field)) {
        missing.add(field);
      }
    }

    assertThat(missing)
        .as(
            "query_chess_games' description must name every filterable field — a client that does"
                + " not read the chessql://reference resource has only this text")
        .isEmpty();
  }

  @Test
  public void theQueryToolDescriptionNamesEveryMotifTheCompilerAccepts() throws Exception {
    String description = descriptionOf("query_chess_games");

    List<String> missing = new ArrayList<>();
    for (String motif : SqlCompiler.motifs()) {
      if (!description.contains(motif)) {
        missing.add(motif);
      }
    }

    assertThat(missing).as("query_chess_games' description must name every motif").isEmpty();
  }

  @Test
  public void theQueryToolDescriptionNamesEveryPerspectiveField() throws Exception {
    String description = descriptionOf("query_chess_games");

    List<String> missing = new ArrayList<>();
    for (String field : SqlCompiler.perspectiveFields()) {
      if (!description.contains(field)) {
        missing.add(field);
      }
    }

    assertThat(missing)
        .as("query_chess_games' description must name every perspective field")
        .isEmpty();
  }

  /**
   * The control. Every assertion above is "no name is missing", which passes trivially against an
   * empty roster or a description this test failed to find.
   */
  @Test
  public void theDescriptionAndTheRostersAreBothNonEmpty() throws Exception {
    assertThat(descriptionOf("query_chess_games")).isNotBlank();
    assertThat(SqlCompiler.filterableFields()).isNotEmpty();
    assertThat(SqlCompiler.motifs()).isNotEmpty();
    assertThat(SqlCompiler.perspectiveFields()).isNotEmpty();
  }

  private static String descriptionOf(String toolName) throws Exception {
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

    for (JsonNode tool : JsonUtils.mapper().readTree(response.body()).at("/result/tools")) {
      if (toolName.equals(tool.get("name").asText())) {
        return tool.get("description").asText();
      }
    }
    throw new AssertionError(toolName + " is not in tools/list");
  }
}
