package com.muchq.chess_indexer.api;

import com.muchq.chess_indexer.model.IndexRequest;

public record IndexRequestResponse(
    String id,
    String platform,
    String username,
    String startDate,
    String endDate,
    String status,
    int gamesIndexed,
    String errorMessage
) {
  public static IndexRequestResponse from(IndexRequest request) {
    return new IndexRequestResponse(
        request.id().toString(),
        request.platform(),
        request.username(),
        request.startDate().toString(),
        request.endDate().toString(),
        request.status().name(),
        request.gamesIndexed(),
        request.errorMessage()
    );
  }
}
