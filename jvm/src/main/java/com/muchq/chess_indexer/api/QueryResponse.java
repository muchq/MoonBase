package com.muchq.chess_indexer.api;

import com.muchq.chess_indexer.model.GameSummary;
import java.util.List;

public record QueryResponse(int count, List<GameSummary> games) {
  public static QueryResponse of(List<GameSummary> games) {
    return new QueryResponse(games.size(), games);
  }
}
