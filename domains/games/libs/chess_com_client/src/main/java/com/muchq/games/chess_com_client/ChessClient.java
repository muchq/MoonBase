package com.muchq.games.chess_com_client;

import com.fasterxml.jackson.databind.ObjectMapper;
import com.muchq.platform.http_client.core.HttpClient;
import com.muchq.platform.http_client.core.HttpRequest;
import com.muchq.platform.http_client.core.HttpResponse;
import java.io.IOException;
import java.time.YearMonth;
import java.time.format.DateTimeFormatter;
import java.util.Optional;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

public class ChessClient {
  private static final Logger LOG = LoggerFactory.getLogger(ChessClient.class);
  private static final String BASE_URL = "https://api.chess.com/pub/player";
  private static final DateTimeFormatter YEAR_MONTH_FORMATTER =
      DateTimeFormatter.ofPattern("yyyy/MM");

  private final HttpClient httpClient;
  private final ObjectMapper mapper;

  public ChessClient(HttpClient httpClient, ObjectMapper objectMapper) {
    this.httpClient = httpClient;
    this.mapper = objectMapper;
  }

  public Optional<Player> fetchPlayer(String player) {
    String url = BASE_URL + "/" + player;
    return getAs(url, Player.class);
  }

  public Optional<StatsResponse> fetchStats(String player) {
    String url = BASE_URL + "/" + player + "/stats";
    return getAs(url, StatsResponse.class);
  }

  public Optional<GamesResponse> fetchGames(String player, YearMonth yearMonth) {
    String url = BASE_URL + "/" + player + "/games/" + yearMonth.format(YEAR_MONTH_FORMATTER);
    return getAs(url, GamesResponse.class);
  }

  private <T> Optional<T> getAs(String url, Class<T> clazz) {
    HttpRequest request = HttpRequest.newBuilder().setUrl(url).build();

    HttpResponse response = httpClient.execute(request);

    if (response.getStatusCode() == 404) {
      return Optional.empty();
    }

    // TODO: Failsafe-ify, 429, etc
    if (response.getStatusCode() != 200) {
      LOG.debug(response.toString());
      throw new RuntimeException("api error");
    }

    try {
      return Optional.of(mapper.readValue(response.getAsInputStream(), clazz));
    } catch (IOException e) {
      throw new RuntimeException(e);
    }
  }
}
