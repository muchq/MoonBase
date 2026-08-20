package com.muchq.games.one_d4.api.dto;

import java.util.UUID;
import org.jspecify.annotations.Nullable;

/**
 * A reanalysis pass as the API reports it. The pass itself runs in the C++ worker; these fields are
 * read off {@code reanalysis_requests}, where the worker checkpoints them.
 */
public record ReanalysisRequestResponse(
    UUID id, String status, int gamesProcessed, int gamesFailed, @Nullable String errorMessage) {}
