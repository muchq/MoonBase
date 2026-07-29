package com.muchq.games.chess_com_client;

/**
 * Thrown when the chess.com API returns a non-200, non-404 response. Carries the HTTP status code
 * so callers can distinguish, e.g., a 429 (back off and retry) from a 503 (upstream outage).
 */
public class ChessComApiException extends RuntimeException {
  private final int statusCode;

  public ChessComApiException(int statusCode, String message) {
    super(message);
    this.statusCode = statusCode;
  }

  public int statusCode() {
    return statusCode;
  }
}
