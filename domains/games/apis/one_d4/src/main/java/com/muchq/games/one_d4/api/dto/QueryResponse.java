package com.muchq.games.one_d4.api.dto;

import java.util.List;

public record QueryResponse(List<GameFeatureRow> games, int count) {}
