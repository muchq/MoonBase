package com.muchq.games.mcpserver.tools;

import static org.assertj.core.api.Assertions.assertThat;

import com.fasterxml.jackson.databind.JsonNode;
import com.muchq.games.chess_com_client.ChessClient;
import com.muchq.games.chess_com_client.ChessComApiException;
import com.muchq.games.chess_com_client.Player;
import com.muchq.platform.json.JsonUtils;
import java.time.Instant;
import java.util.ArrayList;
import java.util.List;
import java.util.Map;
import java.util.Optional;
import java.util.stream.IntStream;
import org.junit.jupiter.api.Test;

public class ChessComPlayersToolTest {

  private static Player player(String username, String title) {
    return new Player(
        1,
        "https://api.chess.com/pub/player/" + username,
        "https://chess.com/member/" + username,
        username,
        username,
        100,
        "https://api.chess.com/pub/country/US",
        Instant.ofEpochSecond(1234567890),
        Instant.ofEpochSecond(1234567890),
        "premium",
        false,
        false,
        "legend",
        List.of(),
        title,
        null,
        null);
  }

  private static class StubChessClient extends ChessClient {
    private final Map<String, Player> players;
    private final List<String> requested = new ArrayList<>();
    private final Map<String, Integer> failures;

    StubChessClient(Map<String, Player> players, Map<String, Integer> failures) {
      super(null, null);
      this.players = players;
      this.failures = failures;
    }

    @Override
    public Optional<Player> fetchPlayer(String username) {
      requested.add(username);
      Integer failStatus = failures.get(username);
      if (failStatus != null) {
        throw new ChessComApiException(failStatus, "HTTP " + failStatus);
      }
      return Optional.ofNullable(players.get(username));
    }
  }

  private static JsonNode parse(String json) {
    return JsonUtils.readAs(json, JsonNode.class);
  }

  @Test
  public void testGetName() {
    var tool = new ChessComPlayersTool(new StubChessClient(Map.of(), Map.of()), JsonUtils.mapper());
    assertThat(tool.getName()).isEqualTo("chess_com_players");
  }

  @Test
  public void testGetInputSchema() {
    var tool = new ChessComPlayersTool(new StubChessClient(Map.of(), Map.of()), JsonUtils.mapper());
    Map<String, Object> schema = tool.getInputSchema();
    @SuppressWarnings("unchecked")
    Map<String, Object> properties = (Map<String, Object>) schema.get("properties");
    assertThat(properties).containsKey("usernames");
  }

  @Test
  public void testBatchLookupKeyedByLowercasedUsername() {
    var stub =
        new StubChessClient(
            Map.of("hikaru", player("hikaru", "GM"), "rpragchess", player("rpragchess", "GM")),
            Map.of());
    var tool = new ChessComPlayersTool(stub, JsonUtils.mapper());

    JsonNode result =
        parse(tool.execute(Map.of("usernames", List.of("Hikaru", "RPRAGCHESS", "ghost"))));

    assertThat(result.get("players").get("hikaru").get("title").asText()).isEqualTo("GM");
    assertThat(result.get("players").get("rpragchess").get("title").asText()).isEqualTo("GM");
    assertThat(result.get("not_found")).hasSize(1);
    assertThat(result.get("not_found").get(0).asText()).isEqualTo("ghost");
    assertThat(result.has("errors")).isFalse();
  }

  @Test
  public void testDuplicatesAreDeduplicated() {
    var stub = new StubChessClient(Map.of("hikaru", player("hikaru", "GM")), Map.of());
    var tool = new ChessComPlayersTool(stub, JsonUtils.mapper());

    tool.execute(Map.of("usernames", List.of("hikaru", "Hikaru", "HIKARU")));

    assertThat(stub.requested).containsExactly("hikaru");
  }

  @Test
  public void testApiErrorForOneUsernameKeepsPartialResults() {
    var stub = new StubChessClient(Map.of("hikaru", player("hikaru", "GM")), Map.of("flaky", 429));
    var tool = new ChessComPlayersTool(stub, JsonUtils.mapper());

    JsonNode result = parse(tool.execute(Map.of("usernames", List.of("hikaru", "flaky"))));

    assertThat(result.get("players").has("hikaru")).isTrue();
    assertThat(result.get("errors").get("flaky").asText()).isEqualTo("HTTP 429");
  }

  @Test
  public void testTooManyUsernamesRejected() {
    var tool = new ChessComPlayersTool(new StubChessClient(Map.of(), Map.of()), JsonUtils.mapper());
    List<String> usernames =
        IntStream.rangeClosed(1, ChessComPlayersTool.MAX_USERNAMES + 1)
            .mapToObj(i -> "user" + i)
            .toList();

    JsonNode result = parse(tool.execute(Map.of("usernames", usernames)));

    assertThat(result.get("error").asText()).contains("too many usernames");
  }

  @Test
  public void testEmptyOrMissingUsernamesRejected() {
    var tool = new ChessComPlayersTool(new StubChessClient(Map.of(), Map.of()), JsonUtils.mapper());
    assertThat(parse(tool.execute(Map.of())).has("error")).isTrue();
    assertThat(parse(tool.execute(Map.of("usernames", List.of()))).has("error")).isTrue();
  }
}
