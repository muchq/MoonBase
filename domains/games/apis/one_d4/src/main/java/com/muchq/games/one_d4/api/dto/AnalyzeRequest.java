package com.muchq.games.one_d4.api.dto;

/**
 * Request for POST /v1/analyze: detect motifs in one PGN without indexing it.
 *
 * <p>Nothing here is persisted, so unlike {@link IndexRequest} there is no player or platform — the
 * PGN is the whole input, and the caller need not be anyone this service has heard of.
 */
public record AnalyzeRequest(String pgn) {}
