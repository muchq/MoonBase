package com.muchq.games.one_d4.api.dto;

import java.util.UUID;
import org.jspecify.annotations.Nullable;

public record IndexResponse(
    UUID id, String status, int gamesIndexed, @Nullable String errorMessage) {}
