package com.muchq.games.mcpserver.tools;

import static org.assertj.core.api.Assertions.assertThat;

import com.fasterxml.jackson.databind.JsonNode;
import com.muchq.games.chess_com_client.ChessClient;
import com.muchq.games.chess_com_client.GamesResponse;
import com.muchq.games.chess_com_client.PlayedGame;
import com.muchq.games.chess_com_client.PlayerResult;
import com.muchq.platform.json.JsonUtils;
import java.time.Instant;
import java.time.YearMonth;
import java.util.List;
import java.util.Map;
import java.util.Optional;
import org.junit.jupiter.api.Test;

public class ChessComGamesToolTest {

  private static class StubChessClient extends ChessClient {
    private final Optional<GamesResponse> response;

    public StubChessClient(Optional<GamesResponse> response) {
      super(null, null);
      this.response = response;
    }

    @Override
    public Optional<GamesResponse> fetchGames(String player, YearMonth yearMonth) {
      return response;
    }
  }

  private static PlayedGame game(
      String url,
      String timeClass,
      String rules,
      boolean rated,
      String whiteUser,
      String blackUser) {
    return new PlayedGame(
        url,
        "[Event \"Live Chess\"]\n1. e4 e5",
        Instant.ofEpochSecond(1234567890),
        rated,
        null,
        "mCvSkBwRnBxE",
        "uuid-" + url,
        "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1",
        "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1",
        timeClass,
        rules,
        new PlayerResult(
            2800,
            "win",
            "https://api.chess.com/pub/player/" + whiteUser,
            whiteUser,
            "w-" + whiteUser),
        new PlayerResult(
            2750,
            "resigned",
            "https://api.chess.com/pub/player/" + blackUser,
            blackUser,
            "b-" + blackUser),
        "https://www.chess.com/openings/Caro-Kann-Defense-Two-Knights-Attack");
  }

  private static final List<PlayedGame> GAMES =
      List.of(
          game("g1", "blitz", "chess", true, "Hikaru", "opp1"),
          game("g2", "blitz", "chess", true, "opp2", "Hikaru"),
          game("g3", "bullet", "chess", true, "Hikaru", "opp1"),
          game("g4", "blitz", "chess960", true, "Hikaru", "opp3"),
          game("g5", "rapid", "chess", false, "opp3", "Hikaru"));

  private static ChessComGamesTool toolFor(List<PlayedGame> games) {
    return new ChessComGamesTool(new StubChessClient(Optional.of(new GamesResponse(games))));
  }

  private final ChessComGamesTool tool = toolFor(GAMES);

  private static JsonNode parse(String json) {
    return JsonUtils.readAs(json, JsonNode.class);
  }

  /**
   * Calls the tool the way the framework does: by name, with the arguments a client sent. The
   * signature is the input schema now, so spreading a map over it here keeps each case below
   * reading as "these arguments, that result" while still exercising the real parameter list.
   */
  private String call(Map<String, Object> arguments) {
    return call(tool, arguments);
  }

  private static String call(ChessComGamesTool tool, Map<String, Object> arguments) {
    var all =
        new java.util.HashMap<String, Object>(
            Map.of("username", "hikaru", "year", "2026", "month", "07"));
    all.putAll(arguments);
    return tool.chessComGames(
        (String) all.get("username"),
        (String) all.get("year"),
        (String) all.get("month"),
        (String) all.get("time_class"),
        (String) all.get("color"),
        (Boolean) all.get("rated"),
        (String) all.get("rules"),
        (String) all.get("opponent"),
        (Boolean) all.get("include_pgn"),
        (Boolean) all.get("include_tcn"),
        (Integer) all.get("limit"),
        (Integer) all.get("offset"));
  }

  @Test
  public void testDefaultExcludesVariantsAndOmitsPgnAndTcn() {
    JsonNode result = parse(call(Map.of()));

    // g4 is chess960 and excluded by the default rules=chess filter
    assertThat(result.get("total_matching").asInt()).isEqualTo(4);
    assertThat(result.get("returned").asInt()).isEqualTo(4);
    assertThat(result.get("has_more").asBoolean()).isFalse();
    for (JsonNode game : result.get("games")) {
      assertThat(game.has("pgn")).isFalse();
      assertThat(game.has("tcn")).isFalse();
      assertThat(game.has("url")).isTrue();
    }
  }

  @Test
  public void testTimeClassFilter() {
    JsonNode result = parse(call(Map.of("time_class", "blitz")));
    assertThat(result.get("total_matching").asInt()).isEqualTo(2);
    for (JsonNode game : result.get("games")) {
      assertThat(game.get("timeClass").asText()).isEqualTo("blitz");
    }
  }

  @Test
  public void testColorFilterIsCaseInsensitive() {
    JsonNode result = parse(call(Map.of("color", "white")));
    // hikaru played white in g1 and g3 (g4 is excluded as a variant)
    assertThat(result.get("total_matching").asInt()).isEqualTo(2);
    for (JsonNode game : result.get("games")) {
      assertThat(game.get("whiteResult").get("username").asText()).isEqualTo("Hikaru");
    }
  }

  @Test
  public void testRatedFilter() {
    JsonNode result = parse(call(Map.of("rated", false)));
    assertThat(result.get("total_matching").asInt()).isEqualTo(1);
    assertThat(result.get("games").get(0).get("url").asText()).isEqualTo("g5");
  }

  @Test
  public void testRulesVariantSelection() {
    JsonNode chess960 = parse(call(Map.of("rules", "chess960")));
    assertThat(chess960.get("total_matching").asInt()).isEqualTo(1);
    assertThat(chess960.get("games").get(0).get("url").asText()).isEqualTo("g4");

    JsonNode all = parse(call(Map.of("rules", "all")));
    assertThat(all.get("total_matching").asInt()).isEqualTo(5);
  }

  @Test
  public void testOpponentFilter() {
    JsonNode result = parse(call(Map.of("opponent", "OPP1")));
    assertThat(result.get("total_matching").asInt()).isEqualTo(2);
    for (JsonNode game : result.get("games")) {
      assertThat(game.get("url").asText()).isIn("g1", "g3");
    }
  }

  @Test
  public void testCombinedFilters() {
    JsonNode result = parse(call(Map.of("time_class", "blitz", "color", "white")));
    assertThat(result.get("total_matching").asInt()).isEqualTo(1);
    assertThat(result.get("games").get(0).get("url").asText()).isEqualTo("g1");
  }

  @Test
  public void testIncludePgnAndTcn() {
    JsonNode result = parse(call(Map.of("include_pgn", true, "include_tcn", true)));
    JsonNode game = result.get("games").get(0);
    assertThat(game.get("pgn").asText()).contains("1. e4 e5");
    assertThat(game.get("tcn").asText()).isNotEmpty();
  }

  @Test
  public void testLimitAndOffsetPagination() {
    JsonNode page1 = parse(call(Map.of("limit", 2)));
    assertThat(page1.get("total_matching").asInt()).isEqualTo(4);
    assertThat(page1.get("returned").asInt()).isEqualTo(2);
    assertThat(page1.get("has_more").asBoolean()).isTrue();

    JsonNode page2 = parse(call(Map.of("limit", 2, "offset", 2)));
    assertThat(page2.get("returned").asInt()).isEqualTo(2);
    assertThat(page2.get("has_more").asBoolean()).isFalse();
    assertThat(page2.get("games").get(0).get("url").asText())
        .isNotEqualTo(page1.get("games").get(0).get("url").asText());
  }

  @Test
  public void testMonthWithAndWithoutLeadingZeroBothWork() {
    assertThat(parse(call(Map.of("month", "7"))).has("games")).isTrue();
    assertThat(parse(call(Map.of("month", "07"))).has("games")).isTrue();
  }

  @Test
  public void testInvalidMonthReturnsUsableError() {
    JsonNode result = parse(call(Map.of("month", "July")));
    assertThat(result.get("error").asText()).contains("month").contains("July");
  }

  @Test
  public void testOutOfRangeMonthReturnsError() {
    JsonNode result = parse(call(Map.of("month", "13")));
    assertThat(result.get("error").asText()).contains("month");
  }

  @Test
  public void testInvalidYearReturnsUsableError() {
    JsonNode result = parse(call(Map.of("year", "invalid")));
    assertThat(result.get("error").asText()).contains("year").contains("invalid");
  }

  @Test
  public void testInvalidTimeClassReturnsError() {
    JsonNode result = parse(call(Map.of("time_class", "hyperbullet")));
    assertThat(result.get("error").asText()).contains("time_class");
  }

  @Test
  public void testInvalidColorReturnsError() {
    JsonNode result = parse(call(Map.of("color", "green")));
    assertThat(result.get("error").asText()).contains("color");
  }

  @Test
  public void testPlayerNotFoundReturnsJsonError() {
    ChessComGamesTool notFoundTool = new ChessComGamesTool(new StubChessClient(Optional.empty()));
    String result = call(notFoundTool, Map.of());
    assertThat(result).isEqualTo("{\"error\":\"player not found\"}");
  }
}
