package com.muchq.games.one_d4.api.dto;

import java.util.List;
import java.util.Map;

/**
 * Response for POST /v2/analyze (served by one_d4_v2).
 *
 * <p>{@code motifs} names the motifs that occurred at least once, lowercased, and is exactly the
 * key set of {@code occurrences} — carried separately so a caller that only wants "what happened in
 * this game" does not have to walk the occurrence lists to find out.
 */
public record AnalyzeResponse(
    int numMoves, List<String> motifs, Map<String, List<AnalyzedOccurrence>> occurrences) {}
