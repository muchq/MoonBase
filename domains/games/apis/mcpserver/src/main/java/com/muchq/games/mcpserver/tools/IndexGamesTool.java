package com.muchq.games.mcpserver.tools;

import com.muchq.games.chess_com_client.ChessComApiException;
import com.muchq.games.one_d4.api.dto.IndexResponse;
import io.micronaut.core.annotation.Nullable;
import io.micronaut.mcp.annotations.Tool;
import io.micronaut.mcp.annotations.ToolArg;
import io.modelcontextprotocol.spec.McpSchema.CallToolResult;
import jakarta.inject.Singleton;

@Singleton
public class IndexGamesTool {

  private final IndexerFacade facade;

  public IndexGamesTool(IndexerFacade facade) {
    this.facade = facade;
  }

  @Tool(
      name = "index_chess_games",
      description =
          "Index a chess player's games for tactical motif detection and ChessQL queries."
              + " Fetches games from chess.com, replays positions, and detects pins, forks,"
              + " skewers, discovered attacks, checks, checkmates, promotions, and more."
              + " Single-month requests complete synchronously; multi-month requests run in the"
              + " background — check progress with index_status. Once indexed, use"
              + " query_chess_games and aggregate_chess_games.")
  public CallToolResult indexChessGames(
      @ToolArg(description = "Chess platform username") String username,
      @ToolArg(description = "Chess platform (currently only chess.com)") String platform,
      @ToolArg(name = "start_month", description = "Start month in YYYY-MM format (e.g. 2026-03)")
          String startMonth,
      @ToolArg(name = "end_month", description = "End month in YYYY-MM format (e.g. 2026-03)")
          String endMonth,
      @Nullable @ToolArg(name = "exclude_bullet", description = "Skip bullet games. Default false")
          Boolean excludeBullet,
      @Nullable
          @ToolArg(
              name = "skip_cache",
              description =
                  "Refetch every month in the range even if it was already indexed, refreshing"
                      + " stored rows (e.g. to backfill titles and opening names on games indexed"
                      + " before those columns existed). Does not start a second run of a range"
                      + " that is already being indexed: if a request for the same player and"
                      + " months is still PENDING/PROCESSING, that request is returned and"
                      + " nothing is refetched, so wait for it to finish and submit again."
                      + " Default false")
          Boolean skipCache) {
    return ToolCallLog.logged(
        "index_chess_games",
        () ->
            doIndexChessGames(username, platform, startMonth, endMonth, excludeBullet, skipCache));
  }

  private CallToolResult doIndexChessGames(
      String username,
      String platform,
      String startMonth,
      String endMonth,
      @Nullable Boolean excludeBullet,
      @Nullable Boolean skipCache) {
    try {
      IndexResponse result =
          facade.index(
              username,
              platform,
              startMonth,
              endMonth,
              Boolean.TRUE.equals(excludeBullet),
              Boolean.TRUE.equals(skipCache));
      return ToolResults.ok(result);
    } catch (IllegalArgumentException | OneD4Client.UpstreamException e) {
      return ToolResults.error(e.getMessage());
    } catch (ChessComApiException e) {
      return ToolResults.error(
          "chess.com API error (HTTP " + e.statusCode() + "): " + e.getMessage());
    }
  }
}
