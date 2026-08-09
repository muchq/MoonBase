package com.muchq.games.mcpserver.tools;

import com.fasterxml.jackson.databind.node.ArrayNode;
import com.fasterxml.jackson.databind.node.ObjectNode;
import com.muchq.games.chessql.parser.ParseException;
import com.muchq.games.one_d4.api.dto.AggregateRow;
import io.micronaut.core.annotation.Nullable;
import io.micronaut.mcp.annotations.Tool;
import io.micronaut.mcp.annotations.ToolArg;
import jakarta.inject.Singleton;
import java.util.List;

@Singleton
public class AggregateGamesTool {

  static final int DEFAULT_LIMIT = 20;
  static final int MAX_LIMIT = 100;

  private final IndexerFacade facade;

  public AggregateGamesTool(IndexerFacade facade) {
    this.facade = facade;
  }

  @Tool(
      name = "aggregate_chess_games",
      description =
          "Count indexed games grouped by one or more fields, filtered by a ChessQL query"
              + " (index first with index_chess_games). This answers questions like 'most popular"
              + " openings' in one call: for one player's openings, query 'white.username ="
              + " \"hikaru\" OR black.username = \"hikaru\"' with group_by [\"opening_family\"] —"
              + " both sides, or you count only the games they had white. The filter may scope"
              + " time with date comparisons ('date >= \"2026-07-01\"') or 'month = \"2026-07\"'; "
              + ToolDescriptions.UNINDEXED_PERIODS_READ_AS_EMPTY
              + " With the player parameter the filter may use perspective"
              + " fields (me.*, opponent.*, outcome) — e.g. player: hikaru with query 'me.color ="
              + " \"white\" AND opponent.title = \"GM\"'. With player set, perspective fields can"
              + " also go in group_by (the group_by description lists what's groupable, keyed by"
              + " underscore forms in the output). Grouping by opponent.title or"
              + " opponent.username is the only correct way to break down opponents across both"
              + " colors — the color-specific columns mix your own values into the buckets on half"
              + " the rows. Untitled opponents group under a null key, and opponent.username"
              + " groups by the stored casing without normalization. me.elo and opponent.elo group"
              + " as fixed-width rating buckets, 100 points unless the term carries a width like"
              + " opponent.elo(200); each group key is the band's numeric lower bound (2400 at"
              + " width 200 means 2400-2599), and NULL elos group under a null key. "
              + ToolDescriptions.OPENING_FAMILY_IS_NOT_NORMALIZED
              + " In the output, count is how many groups were returned, not how many games;"
              + " totalGames/totalGroups cover the untruncated result, and truncated=true means"
              + " the group limit cut off a long tail.")
  public String aggregateChessGames(
      @ToolArg(description = "A ChessQL filter") String query,
      @ToolArg(
              name = "group_by",
              description =
                  "Fields to group by, e.g. [\"opening_family\"]. Groupable: opening_family,"
                      + " opening_name, eco, result, time_class, white_title, black_title,"
                      + " white_username, black_username, platform — plus me.color, me.title,"
                      + " opponent.username, opponent.title, outcome, and the rating buckets"
                      + " me.elo / opponent.elo (optionally with a width, e.g."
                      + " opponent.elo(200); default 100) when player is set, keyed by their"
                      + " underscore forms in the output. date and month are filter-only and"
                      + " rejected here.")
          List<?> groupBy,
      @Nullable
          @ToolArg(
              description =
                  "chess.com username that perspective fields (me.*, opponent.*, outcome) are"
                      + " resolved against; required when the filter uses them, and when group_by"
                      + " uses any perspective field. It does NOT by itself restrict the aggregate"
                      + " to that player: to count only their games, either use a perspective"
                      + " field (in the filter or group_by) or filter explicitly with"
                      + " 'white.username = \"NAME\" OR black.username = \"NAME\"' — the same NAME"
                      + " on both sides, matching this player. Naming a player without either is"
                      + " rejected rather than silently counting other people's games. The filter"
                      + " must be one that cannot match another player's game, so a negation"
                      + " ('NOT white.username = ...'), a '!=' comparison, an OR whose other"
                      + " branch is unrestricted, or an IN list naming anyone else does not"
                      + " qualify")
          String player,
      @Nullable
          @ToolArg(
              description =
                  "Maximum groups to return (default " + DEFAULT_LIMIT + ", max " + MAX_LIMIT + ")")
          Integer limit) {
    if (query.isBlank()) {
      return ToolJson.error("query is required");
    }
    if (groupBy.isEmpty()) {
      return ToolJson.error("group_by must be a non-empty array of field names");
    }
    // List<?>: the binder converts the array but not its elements, so a JSON number would throw
    // ClassCastException out of a declared List<String>.
    List<String> groupByNames = groupBy.stream().map(String::valueOf).toList();
    int effectiveLimit = limit == null ? DEFAULT_LIMIT : Math.min(Math.max(limit, 1), MAX_LIMIT);

    IndexerFacade.AggregateResult aggregate;
    try {
      aggregate = facade.aggregate(query, groupByNames, player, effectiveLimit);
    } catch (ParseException | IllegalArgumentException e) {
      return ToolJson.error(e.getMessage());
    }

    // Built as a node so "groups" stays present even when empty, regardless of the mapper's
    // serialization-inclusion configuration.
    ObjectNode result = ToolJson.object();
    ArrayNode groupsNode = result.putArray("groups");
    for (AggregateRow group : aggregate.groups()) {
      groupsNode.add(ToolJson.mapper().<ObjectNode>valueToTree(group));
    }
    result.put("count", aggregate.groups().size());
    // Untruncated totals: the group limit silently cutting off a long tail (e.g. 103 of 104
    // games) is otherwise invisible to callers.
    result.put("totalGames", aggregate.totalGames());
    result.put("totalGroups", aggregate.totalGroups());
    result.put("truncated", aggregate.totalGroups() > aggregate.groups().size());
    return ToolJson.write(result);
  }
}
