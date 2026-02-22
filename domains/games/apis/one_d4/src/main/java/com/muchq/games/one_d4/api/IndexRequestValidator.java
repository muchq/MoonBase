package com.muchq.games.one_d4.api;

import com.muchq.games.one_d4.api.dto.IndexRequest;
import jakarta.inject.Singleton;
import java.time.YearMonth;
import java.time.format.DateTimeParseException;
import java.time.temporal.ChronoUnit;

@Singleton
public class IndexRequestValidator {

  public void validate(IndexRequest request) {
    if (request.player() == null || request.player().isBlank()) {
      throw new IllegalArgumentException("player is required");
    }
    if (request.platform() == null || request.platform().isBlank()) {
      throw new IllegalArgumentException("platform is required");
    }
    if (!request.platform().equals("CHESS_COM")) {
      throw new IllegalArgumentException(
          "Unsupported platform: " + request.platform() + ". Supported: CHESS_COM");
    }
    YearMonth start = parseMonth(request.startMonth(), "startMonth");
    YearMonth end = parseMonth(request.endMonth(), "endMonth");
    if (start.isAfter(end)) {
      throw new IllegalArgumentException("startMonth must not be after endMonth");
    }
    long monthSpan = start.until(end, ChronoUnit.MONTHS) + 1;
    if (monthSpan > 12) {
      throw new IllegalArgumentException("Maximum range is 12 months, got " + monthSpan);
    }
  }

  YearMonth parseMonth(String value, String fieldName) {
    if (value == null || value.isBlank()) {
      throw new IllegalArgumentException(fieldName + " is required");
    }
    try {
      return YearMonth.parse(value);
    } catch (DateTimeParseException e) {
      throw new IllegalArgumentException(fieldName + " must be in YYYY-MM format, got: " + value);
    }
  }
}
