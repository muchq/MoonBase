package com.muchq.indexer.engine.model;

import java.util.Map;

public record ParsedGame(Map<String, String> headers, String moveText) {
}
