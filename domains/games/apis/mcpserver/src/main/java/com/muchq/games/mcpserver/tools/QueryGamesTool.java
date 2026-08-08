package com.muchq.games.mcpserver.tools;

import com.fasterxml.jackson.databind.node.ArrayNode;
import com.fasterxml.jackson.databind.node.ObjectNode;
import com.muchq.games.chessql.parser.ParseException;
import com.muchq.games.one_d4.api.dto.GameFeatureRow;
import io.micronaut.core.annotation.Nullable;
import io.micronaut.mcp.annotations.Tool;
import io.micronaut.mcp.annotations.ToolArg;
import jakarta.inject.Singleton;
import java.util.List;

@Singleton
public class QueryGamesTool {

  static final int DEFAULT_LIMIT = 10;
  static final int MAX_LIMIT = 50;

  private final IndexerFacade facade;

  public QueryGamesTool(IndexerFacade facade) {
    this.facade = facade;
  }

  @Tool(
      name = "query_chess_games",
      description =
          "Search indexed games using ChessQL (index first with index_chess_games). Example"
              + " queries: 'white.username = \"hikaru\" AND motif(fork)', 'black.title = \"GM\" AND"
              + " opening.family = \"Caro Kann Defense\"', 'eco = \"B90\" AND NOT motif(pin)'."
              + " Available fields: white.elo, black.elo, white.username, black.username,"
              + " white.title, black.title, time.class, eco, opening.name, opening.family, result,"
              + " num.moves, platform, date (ISO comparisons, e.g. 'date >= \"2026-07-01\"'), and"
              + " month (equality only, 'month = \"2026-07\"'). date and month filter the indexed"
              + " corpus only, so a period that was never indexed comes back with zero games"
              + " rather than an error — indistinguishable from 'played no games then' unless you"
              + " index that period first with index_chess_games. Note: opening.family is derived"
              + " from chess.com ECO-URL strings, not a normalized taxonomy — 'Closed Sicilian'"
              + " and 'Closed Sicilian Defense' are distinct values. With the player parameter,"
              + " perspective fields work regardless of color: me.color, me.elo, me.title,"
              + " opponent.username, opponent.elo, opponent.title, and outcome (win/loss/draw) —"
              + " e.g. player: hikaru with 'outcome = \"win\" AND opponent.title = \"GM\"'."
              + " Available motifs: pin, cross_pin, fork, skewer, discovered_attack,"
              + " discovered_check, check, checkmate, promotion, promotion_with_check,"
              + " promotion_with_checkmate, back_rank_mate, smothered_mate, double_check.")
  public String queryChessGames(
      @ToolArg(description = "A ChessQL query string") String query,
      @Nullable
          @ToolArg(
              description =
                  "chess.com username that perspective fields (me.*, opponent.*, outcome) are"
                      + " resolved against; required when the query uses them")
          String player,
      @Nullable
          @ToolArg(
              description =
                  "Maximum games to return (default " + DEFAULT_LIMIT + ", max " + MAX_LIMIT + ")")
          Integer limit,
      @Nullable
          @ToolArg(
              name = "include_pgn",
              description = "Include the full PGN of each game (large). Default false")
          Boolean includePgn) {
    if (query.isBlank()) {
      return ToolJson.error("query is required");
    }
    int effectiveLimit = limit == null ? DEFAULT_LIMIT : Math.min(Math.max(limit, 1), MAX_LIMIT);

    List<GameFeatureRow> games;
    try {
      games = facade.query(query, player, effectiveLimit);
    } catch (ParseException | IllegalArgumentException e) {
      return ToolJson.error(e.getMessage());
    }

    ObjectNode result = ToolJson.object();
    ArrayNode gamesNode = result.putArray("games");
    for (GameFeatureRow game : games) {
      ObjectNode gameNode = ToolJson.mapper().valueToTree(game);
      if (!Boolean.TRUE.equals(includePgn)) {
        gameNode.remove("pgn");
      }
      gamesNode.add(gameNode);
    }
    result.put("count", games.size());

    return ToolJson.write(result);
  }
}
