package com.muchq.games.one_d4.e2e;

import com.fasterxml.jackson.databind.ObjectMapper;
import com.muchq.games.chess_com_client.ChessClient;
import com.muchq.games.chess_com_client.GamesResponse;
import com.muchq.games.chess_com_client.PlayedGame;
import java.time.Instant;
import java.time.YearMonth;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import java.util.Optional;

/**
 * Fake chess.com API client for local e2e tests. Returns configurable game lists per (player,
 * month) and records all fetchGames calls for assertions.
 */
public final class FakeChessClient extends ChessClient {

  private static final String MINIMAL_PGN =
      """
      [Event "Live Chess"]
      [Site "Chess.com"]
      [White "White"]
      [Black "Black"]
      [Result "1-0"]
      [ECO "B20"]

      1. e4 e5 2. Nf3 Nc6 3. Bb5 a6 1-0
      """;

  private final Map<String, List<PlayedGame>> responsesByKey = new HashMap<>();
  private final List<FetchCall> fetchCalls = new ArrayList<>();

  public FakeChessClient() {
    super(null, new ObjectMapper());
  }

  /** Registers games to return for the given player and month. */
  public void setGames(String player, YearMonth month, List<PlayedGame> games) {
    responsesByKey.put(key(player, month), new ArrayList<>(games));
  }

  /** Adds one game with a unique URL; returns a minimal valid PGN. */
  public void addGame(String player, YearMonth month, String gameUrl) {
    responsesByKey
        .computeIfAbsent(key(player, month), k -> new ArrayList<>())
        .add(minimalPlayedGame(gameUrl));
  }

  /** Sets an empty response for the given player and month. */
  public void setNoGames(String player, YearMonth month) {
    responsesByKey.put(key(player, month), List.of());
  }

  @Override
  public Optional<GamesResponse> fetchGames(String player, YearMonth yearMonth) {
    fetchCalls.add(new FetchCall(player, yearMonth));
    String k = key(player, yearMonth);
    List<PlayedGame> games = responsesByKey.get(k);
    if (games == null) {
      return Optional.empty();
    }
    return Optional.of(new GamesResponse(games));
  }

  public List<FetchCall> getFetchCalls() {
    return new ArrayList<>(fetchCalls);
  }

  public void clearFetchCalls() {
    fetchCalls.clear();
  }

  private static String key(String player, YearMonth month) {
    return player + "|" + month;
  }

  public static PlayedGame minimalPlayedGame(String gameUrl) {
    return new PlayedGame(
        gameUrl,
        MINIMAL_PGN,
        Instant.EPOCH,
        true,
        new com.muchq.games.chess_com_client.Accuracies(90.0, 85.0),
        "",
        "uuid-" + gameUrl.hashCode(),
        "",
        "",
        "blitz",
        "chess",
        new com.muchq.games.chess_com_client.PlayerResult(
            1500, "win", "https://chess.com/white", "White", "uuid-w"),
        new com.muchq.games.chess_com_client.PlayerResult(
            1500, "loss", "https://chess.com/black", "Black", "uuid-b"),
        "B20");
  }

  public record FetchCall(String player, YearMonth month) {}
}
