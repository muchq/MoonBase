package com.muchq.games.mcpserver.tools;

import static com.muchq.games.mcpserver.tools.ToolResultText.errorPayloadOf;
import static com.muchq.games.mcpserver.tools.ToolResultText.payloadOf;
import static org.assertj.core.api.Assertions.assertThat;

import com.muchq.games.one_d4.api.dto.AggregateRow;
import com.muchq.platform.json.JsonUtils;
import java.util.ArrayList;
import java.util.List;
import java.util.Map;
import org.junit.jupiter.api.Test;

/**
 * What {@code aggregate_chess_games} hands the facade.
 *
 * <p>The layers on either side were already pinned and the seam between them was not: {@code
 * IndexerFacadeHttpTest} proves the facade puts {@code orderBy} / {@code minGames} on the wire, and
 * {@code McpProtocolTest} proves the tool advertises the argument <em>names</em>. Neither says the
 * tool passes the values it received. Dropping both arguments from the facade call left every suite
 * green — {@code McpSharedCorpusE2ETest} exercises a one-group corpus, where a ranking and a floor
 * of 1 are invisible, and the metrics it asserts come from {@code player} rather than from either
 * argument. Found by review on #1368; this is the assertion that kills it.
 *
 * <p>A recording facade rather than a stub HTTP server on purpose: the question here is only which
 * values cross this call, and a server would answer it through two more layers that have their own
 * tests.
 */
public class AggregateGamesToolTest {

  @Test
  public void rankingArgumentsReachTheFacade() {
    RecordingFacade facade = new RecordingFacade();

    payloadOf(
        new AggregateGamesTool(facade)
            .aggregateChessGames(
                "outcome = \"win\"", List.of("opening_family"), "hikaru", 25, "score", 10));

    assertThat(facade.calls).hasSize(1);
    Call call = facade.calls.get(0);
    assertThat(call.orderBy).as("the ranking the caller asked for").isEqualTo("score");
    assertThat(call.minGames).as("the evidence floor the caller asked for").isEqualTo(10);
    assertThat(call.player).isEqualTo("hikaru");
    assertThat(call.limit).isEqualTo(25);
    assertThat(call.groupBy).containsExactly("opening_family");
  }

  /** Omitted arguments travel as the defaults, not as something the facade has to guess at. */
  @Test
  public void omittedRankingArgumentsBecomeTheDefaults() {
    RecordingFacade facade = new RecordingFacade();

    payloadOf(
        new AggregateGamesTool(facade)
            .aggregateChessGames("white.elo > 1", List.of("eco"), null, null, null, null));

    Call call = facade.calls.get(0);
    assertThat(call.orderBy).isNull();
    assertThat(call.minGames).isZero();
    assertThat(call.limit).isEqualTo(AggregateGamesTool.DEFAULT_LIMIT);
  }

  /**
   * A negative floor is not a different request from no floor, and it must not reach one_d4 as one
   * — the same clamp the endpoint applies, applied before the round trip.
   */
  @Test
  public void aNegativeFloorIsClampedRatherThanForwarded() {
    RecordingFacade facade = new RecordingFacade();

    payloadOf(
        new AggregateGamesTool(facade)
            .aggregateChessGames("white.elo > 1", List.of("eco"), null, 20, "count", -5));

    assertThat(facade.calls.get(0).minGames).isZero();
  }

  /** The limit is still clamped to the tool's ceiling, which the new arguments must not disturb. */
  @Test
  public void theLimitIsStillClampedAroundTheRankingArguments() {
    RecordingFacade facade = new RecordingFacade();

    payloadOf(
        new AggregateGamesTool(facade)
            .aggregateChessGames("white.elo > 1", List.of("eco"), "hikaru", 5000, "count", 2));

    assertThat(facade.calls.get(0).limit).isEqualTo(AggregateGamesTool.MAX_LIMIT);
    assertThat(facade.calls.get(0).minGames).isEqualTo(2);
  }

  /**
   * one_d4 owns the ranking vocabulary, so a rejection comes back as its 400 message. The tool must
   * report it rather than swallowing it — and must not have invented a second copy of the rules to
   * check against first.
   */
  @Test
  public void anUpstreamRejectionOfTheRankingIsReportedVerbatim() throws Exception {
    RecordingFacade facade = new RecordingFacade();
    facade.failure =
        new IllegalArgumentException("orderBy \"score\" requires a player: score is that player's");

    String error =
        errorPayloadOf(
            new AggregateGamesTool(facade)
                .aggregateChessGames("white.elo > 1", List.of("eco"), null, 20, "score", 0));

    // Read the field rather than the raw text: the message is JSON-escaped on the way out, so a
    // substring assertion would pass on a mangled message as readily as on an intact one.
    assertThat(JsonUtils.mapper().readTree(error).get("error").asText())
        .isEqualTo("orderBy \"score\" requires a player: score is that player's");
  }

  /** The outcome metrics survive into the tool's payload rather than being flattened to counts. */
  @Test
  public void outcomeMetricsReachThePayload() {
    RecordingFacade facade = new RecordingFacade();
    facade.result =
        new IndexerFacade.AggregateResult(
            List.of(
                AggregateRow.withOutcomes(Map.of("opening_family", "Caro Kann"), 41, 15, 20, 6)),
            41,
            1);

    String payload =
        payloadOf(
            new AggregateGamesTool(facade)
                .aggregateChessGames(
                    "outcome = \"win\"", List.of("opening_family"), "hikaru", 20, "score", 5));

    assertThat(payload)
        .contains("\"wins\":15")
        .contains("\"losses\":20")
        .contains("\"draws\":6")
        .contains("\"score\":18.0");
  }

  private record Call(
      String query, List<String> groupBy, String player, int limit, String orderBy, int minGames) {}

  /** Records what the tool passes, and answers with whatever the test set up. */
  private static final class RecordingFacade extends IndexerFacade {

    private final List<Call> calls = new ArrayList<>();
    private IndexerFacade.AggregateResult result =
        new IndexerFacade.AggregateResult(List.of(), 0, 0);
    private RuntimeException failure;

    private RecordingFacade() {
      // No client: every call this test makes is intercepted below, so nothing reaches HTTP. A
      // method that forgot to override would NPE here rather than quietly opening a connection.
      super(null);
    }

    @Override
    public AggregateResult aggregate(
        String chessql,
        List<String> groupBy,
        String player,
        int limit,
        String orderBy,
        int minGames) {
      calls.add(new Call(chessql, groupBy, player, limit, orderBy, minGames));
      if (failure != null) {
        throw failure;
      }
      return result;
    }
  }
}
