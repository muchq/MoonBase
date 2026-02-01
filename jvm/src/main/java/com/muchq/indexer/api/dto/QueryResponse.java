package com.muchq.indexer.api.dto;

import java.util.List;

public record QueryResponse(List<GameFeatureRow> games, int count) {
}
