package com.muchq.games.chess_com_client;

import com.fasterxml.jackson.databind.ObjectMapper;
import com.muchq.platform.http_client.core.HttpClient;
import com.muchq.platform.http_client.core.HttpRequest;
import com.muchq.platform.http_client.core.HttpResponse;
import dev.failsafe.Failsafe;
import dev.failsafe.FailsafeException;
import dev.failsafe.Timeout;
import dev.failsafe.TimeoutExceededException;
import java.io.IOException;
import java.time.Duration;
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

  /**
   * Deadline for one call to chess.com, covering the whole exchange.
   *
   * <p>Generous, because a month archive with full PGNs is a real download on a slow link and
   * cutting off a working request is its own failure. The point is that there <em>is</em> a
   * ceiling: without one, a peer that sends a response head and then stops parks the caller
   * forever, and here the caller is an indexing worker holding a request lease — so the row stays
   * owned by a thread that is never coming back, and the attempt it spent is gone (#1336).
   */
  public static final Duration DEFAULT_TIMEOUT = Duration.ofSeconds(60);

  /**
   * chess.com did not answer in time. Reported as a gateway timeout rather than a real status
   * because callers already branch on {@link ChessComApiException}, and a stalled upstream belongs
   * with the other transient upstream failures rather than in a category of its own.
   */
  private static final int TIMEOUT_STATUS = 504;

  private final HttpClient httpClient;
  private final ObjectMapper mapper;
  private final Duration timeout;
  private final Timeout<Object> deadline;

  public ChessClient(HttpClient httpClient, ObjectMapper objectMapper) {
    this(httpClient, objectMapper, DEFAULT_TIMEOUT);
  }

  public ChessClient(HttpClient httpClient, ObjectMapper objectMapper, Duration timeout) {
    this.httpClient = httpClient;
    this.mapper = objectMapper;
    this.timeout = timeout;
    // withInterrupt is the load-bearing half: without it Failsafe marks the execution failed and
    // leaves the thread parked in the body read, which is the bug rather than the fix.
    this.deadline = Timeout.builder(timeout).withInterrupt().build();
  }

  public Optional<Player> fetchPlayer(String player) {
    String url = BASE_URL + "/" + player.toLowerCase();
    return getAs(url, Player.class);
  }

  public Optional<StatsResponse> fetchStats(String player) {
    String url = BASE_URL + "/" + player.toLowerCase() + "/stats";
    return getAs(url, StatsResponse.class);
  }

  public Optional<GamesResponse> fetchGames(String player, YearMonth yearMonth) {
    String url =
        BASE_URL + "/" + player.toLowerCase() + "/games/" + yearMonth.format(YEAR_MONTH_FORMATTER);
    return getAs(url, GamesResponse.class);
  }

  /**
   * One deadline over the request, the status check and the body read.
   *
   * <p>The transport timeout below covers only time-to-headers — the JDK's request deadline ends
   * when the response head arrives and the body is streamed after that. Wrapping the whole thing
   * here is what bounds a peer that answers and then stalls mid-body.
   *
   * <p>TODO: retry with backoff on 429/5xx, which is the other half of Failsafe-ifying this.
   */
  private <T> Optional<T> getAs(String url, Class<T> clazz) {
    try {
      return Failsafe.with(deadline).get(() -> exchange(url, clazz));
    } catch (TimeoutExceededException e) {
      throw new ChessComApiException(
          TIMEOUT_STATUS, "chess.com did not answer within " + timeout + " for " + url);
    } catch (FailsafeException e) {
      Throwable cause = e.getCause();
      if (cause instanceof RuntimeException runtime) {
        throw runtime;
      }
      throw new RuntimeException(cause == null ? e : cause);
    }
  }

  private <T> Optional<T> exchange(String url, Class<T> clazz) throws IOException {
    HttpRequest request =
        HttpRequest.newBuilder().setUrl(url).setResponseHeadersTimeout(timeout).build();

    HttpResponse response = httpClient.execute(request);

    if (response.getStatusCode() == 404) {
      // Drain before returning: an unread body holds its connection out of the pool, and a
      // not-found lookup is common enough here to leak one per miss.
      discardBody(response);
      return Optional.empty();
    }

    if (response.getStatusCode() != 200) {
      LOG.debug(response.toString());
      throw new ChessComApiException(
          response.getStatusCode(),
          "chess.com API returned HTTP " + response.getStatusCode() + " for " + url);
    }

    return Optional.of(mapper.readValue(response.getAsInputStream(), clazz));
  }

  private static void discardBody(HttpResponse response) {
    try {
      response.getAsBytes();
    } catch (RuntimeException e) {
      // Nothing to salvage; the status was the whole answer.
    }
  }
}
