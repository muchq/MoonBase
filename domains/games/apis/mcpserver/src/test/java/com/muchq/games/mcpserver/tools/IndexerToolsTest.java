package com.muchq.games.mcpserver.tools;

import static org.assertj.core.api.Assertions.assertThat;

import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.ObjectMapper;
import com.muchq.games.chess_com_client.ChessClient;
import com.muchq.games.chess_com_client.GamesResponse;
import com.muchq.games.chess_com_client.PlayedGame;
import com.muchq.games.chess_com_client.Player;
import com.muchq.games.chess_com_client.PlayerResult;
import com.muchq.games.chessql.compiler.SqlCompiler;
import com.muchq.games.one_d4.db.GameFeatureDao;
import com.muchq.games.one_d4.db.IndexedPeriodDao;
import com.muchq.games.one_d4.db.IndexingRequestDao;
import com.muchq.games.one_d4.db.TestDb;
import com.muchq.games.one_d4.engine.FeatureExtractor;
import com.muchq.games.one_d4.engine.GameReplayer;
import com.muchq.games.one_d4.engine.PgnParser;
import com.muchq.games.one_d4.motifs.AttackDetector;
import com.muchq.games.one_d4.motifs.CheckDetector;
import com.muchq.games.one_d4.motifs.PinDetector;
import com.muchq.games.one_d4.queue.InMemoryIndexQueue;
import com.muchq.games.one_d4.service.IndexRequestService;
import com.muchq.games.one_d4.worker.IndexWorker;
import com.muchq.platform.json.JsonUtils;
import java.time.Instant;
import java.time.YearMonth;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import java.util.Optional;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;
import org.junit.jupiter.api.AfterEach;
import org.junit.jupiter.api.BeforeEach;
import org.junit.jupiter.api.Test;

/**
 * Exercises the in-process indexer MCP tools end to end: index a month from a stubbed chess.com
 * client into H2, then query, aggregate, and analyze through the tool layer.
 */
public class IndexerToolsTest {

  private static final String SCHOLARS_MATE_PGN =
      """
      [Event "Live Chess"]
      [Site "Chess.com"]
      [White "Hikaru"]
      [Black "someuser"]
      [Result "1-0"]
      [ECO "C20"]

      1. e4 e5 2. Qh5 Nc6 3. Bc4 Nf6 4. Qxf7# 1-0
      """;

  private final ObjectMapper mapper = JsonUtils.mapper();

  private TestDb testDb;
  private StubChessClient chessClient;
  private InMemoryIndexQueue queue;
  private ExecutorService executor;
  private IndexerFacade facade;

  private IndexGamesTool indexTool;
  private IndexStatusTool statusTool;
  private QueryGamesTool queryTool;
  private AggregateGamesTool aggregateTool;
  private AnalyzePositionTool analyzeTool;

  @BeforeEach
  public void setUp() {
    testDb = TestDb.create("mcp_indexer");
    chessClient = new StubChessClient();
    queue = new InMemoryIndexQueue();
    executor = Executors.newFixedThreadPool(2);

    FeatureExtractor extractor =
        new FeatureExtractor(
            new PgnParser(),
            new GameReplayer(),
            List.of(new PinDetector(), new CheckDetector(), new AttackDetector()));
    IndexingRequestDao requestDao = new IndexingRequestDao(testDb.jdbi());
    GameFeatureDao gameFeatureDao = new GameFeatureDao(testDb.jdbi(), true);
    IndexWorker worker =
        new IndexWorker(
            chessClient,
            extractor,
            requestDao,
            gameFeatureDao,
            new IndexedPeriodDao(testDb.jdbi(), true),
            executor);
    IndexRequestService indexRequestService =
        new IndexRequestService(requestDao, queue, worker::process);
    facade = new IndexerFacade(indexRequestService, gameFeatureDao, extractor, new SqlCompiler());

    indexTool = new IndexGamesTool(facade, mapper);
    statusTool = new IndexStatusTool(facade, mapper);
    queryTool = new QueryGamesTool(facade, mapper);
    aggregateTool = new AggregateGamesTool(facade, mapper);
    analyzeTool = new AnalyzePositionTool(facade, mapper);
  }

  @AfterEach
  public void tearDown() {
    executor.shutdownNow();
  }

  private JsonNode parse(String json) {
    return JsonUtils.readAs(json, JsonNode.class);
  }

  private void givenIndexedMonth() {
    chessClient.setTitle("hikaru", "GM");
    chessClient.setGames(
        YearMonth.of(2026, 6),
        List.of(
            game(
                "https://chess.com/game/1",
                "Hikaru",
                "someuser",
                "https://www.chess.com/openings/Kings-Pawn-Opening-Wayward-Queen-Attack"),
            game(
                "https://chess.com/game/2",
                "someuser",
                "Hikaru",
                "https://www.chess.com/openings/Caro-Kann-Defense-Two-Knights-Attack")));

    JsonNode result =
        parse(
            indexTool.execute(
                Map.of(
                    "username", "hikaru",
                    "platform", "chess.com",
                    "start_month", "2026-06",
                    "end_month", "2026-06")));
    assertThat(result.get("status").asText()).isEqualTo("COMPLETED");
    assertThat(result.get("gamesIndexed").asInt()).isEqualTo(2);
  }

  @Test
  public void singleMonthIndexRunsSynchronouslyAndIsQueryable() {
    givenIndexedMonth();

    JsonNode queried =
        parse(
            queryTool.execute(
                Map.of("query", "white.username = \"hikaru\" AND motif(check)", "limit", 10)));
    assertThat(queried.get("count").asInt()).isEqualTo(1);
    JsonNode game = queried.get("games").get(0);
    assertThat(game.get("whiteTitle").asText()).isEqualTo("GM");
    assertThat(game.get("openingFamily").asText()).isEqualTo("Kings Pawn Opening");
    assertThat(game.has("pgn")).isFalse();
  }

  @Test
  public void queryByTitleAndOpeningFamilyFields() {
    givenIndexedMonth();

    JsonNode byTitle =
        parse(queryTool.execute(Map.of("query", "black.title = \"GM\"", "limit", 10)));
    assertThat(byTitle.get("count").asInt()).isEqualTo(1);
    assertThat(byTitle.get("games").get(0).get("blackUsername").asText()).isEqualTo("Hikaru");

    JsonNode byFamily =
        parse(
            queryTool.execute(
                Map.of("query", "opening.family = \"caro kann defense\"", "limit", 10)));
    assertThat(byFamily.get("count").asInt()).isEqualTo(1);
  }

  @Test
  public void queryIncludePgnKeepsPgn() {
    givenIndexedMonth();
    JsonNode queried =
        parse(
            queryTool.execute(Map.of("query", "white.username = \"hikaru\"", "include_pgn", true)));
    assertThat(queried.get("games").get(0).get("pgn").asText()).contains("1. e4 e5");
  }

  @Test
  public void aggregateGroupsByOpeningFamily() {
    givenIndexedMonth();

    JsonNode result =
        parse(
            aggregateTool.execute(
                Map.of(
                    "query",
                    "white.username = \"hikaru\" OR black.username = \"hikaru\"",
                    "group_by",
                    List.of("opening_family"))));

    assertThat(result.get("count").asInt()).isEqualTo(2);
    for (JsonNode group : result.get("groups")) {
      assertThat(group.get("group").get("opening_family").asText())
          .isIn("Kings Pawn Opening", "Caro Kann Defense");
      assertThat(group.get("count").asInt()).isEqualTo(1);
    }
  }

  @Test
  public void indexStatusReturnsRequest() {
    givenIndexedMonth();
    // Re-issuing the same request returns the existing one
    JsonNode result =
        parse(
            indexTool.execute(
                Map.of(
                    "username", "hikaru",
                    "platform", "chess.com",
                    "start_month", "2026-06",
                    "end_month", "2026-06")));
    JsonNode status = parse(statusTool.execute(Map.of("request_id", result.get("id").asText())));
    assertThat(status.get("status").asText()).isEqualTo("COMPLETED");
  }

  @Test
  public void skipCacheForcesRefetchAndCaseInsensitiveDedupe() {
    givenIndexedMonth();
    assertThat(chessClient.getFetchGamesCalls()).isEqualTo(1);

    // Without skip_cache, a re-request (any username case) is served from the period cache
    JsonNode cached =
        parse(
            indexTool.execute(
                Map.of(
                    "username", "HIKARU",
                    "platform", "chess.com",
                    "start_month", "2026-06",
                    "end_month", "2026-06")));
    assertThat(cached.get("player").asText()).isEqualTo("hikaru");
    assertThat(cached.get("status").asText()).isEqualTo("COMPLETED");
    assertThat(chessClient.getFetchGamesCalls()).isEqualTo(1);

    // With skip_cache, the month is refetched and rows rewritten
    JsonNode refetched =
        parse(
            indexTool.execute(
                Map.of(
                    "username", "hikaru",
                    "platform", "chess.com",
                    "start_month", "2026-06",
                    "end_month", "2026-06",
                    "skip_cache", true)));
    assertThat(refetched.get("status").asText()).isEqualTo("COMPLETED");
    assertThat(refetched.get("gamesIndexed").asInt()).isEqualTo(2);
    assertThat(chessClient.getFetchGamesCalls()).isEqualTo(2);
  }

  @Test
  public void multiMonthIndexIsEnqueuedAsPending() {
    JsonNode result =
        parse(
            indexTool.execute(
                Map.of(
                    "username", "hikaru",
                    "platform", "chess.com",
                    "start_month", "2026-01",
                    "end_month", "2026-03")));
    assertThat(result.get("status").asText()).isEqualTo("PENDING");
    assertThat(queue.size()).isEqualTo(1);
  }

  @Test
  public void perspectiveFieldsResolveAgainstPlayerParam() {
    givenIndexedMonth();

    // hikaru wins game/1 as white (1-0) and loses game/2 as black (1-0)
    JsonNode wins =
        parse(
            queryTool.execute(
                Map.of("query", "outcome = \"win\"", "player", "hikaru", "limit", 10)));
    assertThat(wins.get("count").asInt()).isEqualTo(1);
    assertThat(wins.get("games").get(0).get("gameUrl").asText())
        .isEqualTo("https://chess.com/game/1");

    JsonNode lossesByFamily =
        parse(
            aggregateTool.execute(
                Map.of(
                    "query",
                    "outcome = \"loss\"",
                    "player",
                    "hikaru",
                    "group_by",
                    List.of("opening_family"))));
    assertThat(lossesByFamily.get("count").asInt()).isEqualTo(1);
    assertThat(lossesByFamily.get("groups").get(0).get("group").get("opening_family").asText())
        .isEqualTo("Caro Kann Defense");

    JsonNode missingPlayer = parse(queryTool.execute(Map.of("query", "outcome = \"win\"")));
    assertThat(missingPlayer.get("error").asText()).contains("requires a player");
  }

  @Test
  public void aggregateWithNoMatchesReturnsEmptyGroupsArray() {
    JsonNode result =
        parse(
            aggregateTool.execute(
                Map.of(
                    "query",
                    "white.username = \"nobody\"",
                    "group_by",
                    List.of("opening_family"))));
    assertThat(result.get("groups").isArray()).isTrue();
    assertThat(result.get("groups")).isEmpty();
    assertThat(result.get("count").asInt()).isZero();
  }

  @Test
  public void analyzePositionDetectsMotifsWithoutIndexing() {
    JsonNode result = parse(analyzeTool.execute(Map.of("pgn", SCHOLARS_MATE_PGN)));
    assertThat(result.get("numMoves").asInt()).isEqualTo(4);
    List<String> motifs = new ArrayList<>();
    result.get("motifs").forEach(m -> motifs.add(m.asText()));
    assertThat(motifs).contains("check");
    assertThat(motifs).doesNotContain("attack");
    assertThat(result.get("occurrences").has("check")).isTrue();
  }

  @Test
  public void invalidMonthReturnsJsonError() {
    JsonNode result =
        parse(
            indexTool.execute(
                Map.of(
                    "username", "hikaru",
                    "platform", "chess.com",
                    "start_month", "June",
                    "end_month", "2026-06")));
    assertThat(result.get("error").asText()).contains("YYYY-MM");
  }

  @Test
  public void unsupportedPlatformReturnsJsonError() {
    JsonNode result =
        parse(
            indexTool.execute(
                Map.of(
                    "username", "x",
                    "platform", "lichess",
                    "start_month", "2026-06",
                    "end_month", "2026-06")));
    assertThat(result.get("error").asText()).contains("Unsupported platform");
  }

  @Test
  public void badChessQlReturnsJsonError() {
    JsonNode result = parse(queryTool.execute(Map.of("query", "motif(")));
    assertThat(result.has("error")).isTrue();

    JsonNode unknownField =
        parse(aggregateTool.execute(Map.of("query", "white.elo > 1", "group_by", List.of("pgn"))));
    assertThat(unknownField.get("error").asText()).contains("Unknown field");
  }

  @Test
  public void unknownRequestIdReturnsJsonError() {
    JsonNode badUuid = parse(statusTool.execute(Map.of("request_id", "not-a-uuid")));
    assertThat(badUuid.get("error").asText()).contains("UUID");

    JsonNode missing =
        parse(statusTool.execute(Map.of("request_id", "00000000-0000-0000-0000-000000000000")));
    assertThat(missing.get("error").asText()).contains("not found");
  }

  private static PlayedGame game(String url, String white, String black, String ecoUrl) {
    return new PlayedGame(
        url,
        SCHOLARS_MATE_PGN,
        Instant.ofEpochSecond(1750000000),
        true,
        null,
        "",
        "uuid-" + url.hashCode(),
        "",
        "",
        "blitz",
        "chess",
        new PlayerResult(2800, "win", "https://chess.com/w", white, "uuid-w"),
        new PlayerResult(1500, "checkmated", "https://chess.com/b", black, "uuid-b"),
        ecoUrl);
  }

  private static final class StubChessClient extends ChessClient {
    private final Map<YearMonth, List<PlayedGame>> gamesByMonth = new HashMap<>();
    private final Map<String, String> titles = new HashMap<>();
    private int fetchGamesCalls = 0;

    StubChessClient() {
      super(null, new ObjectMapper());
    }

    void setGames(YearMonth month, List<PlayedGame> games) {
      gamesByMonth.put(month, games);
    }

    void setTitle(String username, String title) {
      titles.put(username.toLowerCase(), title);
    }

    int getFetchGamesCalls() {
      return fetchGamesCalls;
    }

    @Override
    public Optional<GamesResponse> fetchGames(String player, YearMonth yearMonth) {
      fetchGamesCalls++;
      List<PlayedGame> games = gamesByMonth.get(yearMonth);
      return games == null
          ? Optional.of(new GamesResponse(List.of()))
          : Optional.of(new GamesResponse(games));
    }

    @Override
    public Optional<Player> fetchPlayer(String username) {
      String title = titles.get(username.toLowerCase());
      if (title == null) {
        return Optional.empty();
      }
      return Optional.of(
          new Player(
              0,
              null,
              null,
              null,
              username,
              0,
              null,
              Instant.EPOCH,
              Instant.EPOCH,
              null,
              false,
              false,
              null,
              List.of(),
              title,
              null,
              null));
    }
  }
}
