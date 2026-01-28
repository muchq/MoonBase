package com.muchq.chess_indexer.api;

public record IndexRequestCreateRequest(
    String platform,
    String username,
    String startDate,
    String endDate
) {}
