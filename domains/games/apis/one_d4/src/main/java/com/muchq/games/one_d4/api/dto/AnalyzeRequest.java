package com.muchq.games.one_d4.api.dto;

/**
 * Request for POST /v2/analyze (served by one_d4_v2): detect motifs in one PGN without indexing it.
 * The DTO lives here because mcpserver deserializes with one_d4's own types (#1332).
 *
 * <p>Nothing here is persisted, so unlike {@link IndexRequest} there is no player or platform — the
 * PGN is the whole input, and the caller need not be anyone this service has heard of.
 */
public record AnalyzeRequest(String pgn) {}
