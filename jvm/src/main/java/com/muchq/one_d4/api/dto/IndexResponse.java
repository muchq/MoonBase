package com.muchq.one_d4.api.dto;

import java.util.UUID;

public record IndexResponse(UUID id, String status, int gamesIndexed, String errorMessage) {
}
