package com.muchq.one_d4.engine.model;

import java.util.Map;

public record ParsedGame(Map<String, String> headers, String moveText) {
}
