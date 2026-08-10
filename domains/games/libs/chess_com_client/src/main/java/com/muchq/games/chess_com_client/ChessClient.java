package com.muchq.games.chess_com_client;

import com.fasterxml.jackson.databind.ObjectMapper;
import com.muchq.platform.http_client.core.HttpClient;
import com.muchq.platform.http_client.core.HttpRequest;
import com.muchq.platform.http_client.core.HttpResponse;
import java.io.IOException;
import java.io.UncheckedIOException;
import java.net.http.HttpTimeoutException;
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
  private final String baseUrl;

  public ChessClient(HttpClient httpClient, ObjectMapper objectMapper) {
    this(httpClient, objectMapper, DEFAULT_TIMEOUT);
  }

  public ChessClient(HttpClient httpClient, ObjectMapper objectMapper, Duration timeout) {
    this(httpClient, objectMapper, timeout, BASE_URL);
  }

  /**
   * Package-private, and the only reason it exists: the deadline can only be tested against a peer
   * that actually stalls, and chess.com will not. The test that claims to cover it built exactly
   * such a server and then had no way to aim the client at it, so it called the public API instead
   * and passed or failed on how fast chess.com answered that day.
   *
   * <p>Not public. chess.com's base URL is not a deployment decision and nothing outside this
   * package has a reason to change it.
   */
  ChessClient(HttpClient httpClient, ObjectMapper objectMapper, Duration timeout, String baseUrl) {
    this.httpClient = httpClient;
    this.mapper = objectMapper;
    this.timeout = timeout;
    this.baseUrl = baseUrl;
  }

  public Optional<Player> fetchPlayer(String player) {
    String url = baseUrl + "/" + player.toLowerCase();
    return getAs(url, Player.class);
  }

  public Optional<StatsResponse> fetchStats(String player) {
    String url = baseUrl + "/" + player.toLowerCase() + "/stats";
    return getAs(url, StatsResponse.class);
  }

  public Optional<GamesResponse> fetchGames(String player, YearMonth yearMonth) {
    String url =
        baseUrl + "/" + player.toLowerCase() + "/games/" + yearMonth.format(YEAR_MONTH_FORMATTER);
    return getAs(url, GamesResponse.class);
  }

  /**
   * Translation only. The deadline itself belongs to the shared client, which bounds the whole
   * exchange — send, head and body alike — and reports an expired one as {@link
   * HttpTimeoutException}.
   *
   * <p>This used to run the exchange inside a Failsafe {@code Timeout} as well, back when the
   * transport deadline stopped at the response head and something had to cover the body read
   * (#1336). Two deadlines of the same length over one call is worse than one: they race, and the
   * caller got {@link ChessComApiException} or {@link UncheckedIOException} depending on which
   * scheduler thread happened to fire first. Worse, Failsafe's {@code withInterrupt} bounded the
   * call by interrupting this thread, so an ordinary slow upstream left the interrupt status set —
   * and IndexWorker reads that status to decide a run was cancelled, so a timeout here could
   * abandon a request instead of recording a failed attempt against it.
   *
   * <p>TODO: retry with backoff on 429/5xx. That is a real use for Failsafe here; a second timeout
   * was not.
   */
  private <T> Optional<T> getAs(String url, Class<T> clazz) {
    try {
      return exchange(url, clazz);
    } catch (UncheckedIOException e) {
      if (e.getCause() instanceof HttpTimeoutException) {
        throw new ChessComApiException(
            TIMEOUT_STATUS, "chess.com did not answer within " + timeout + " for " + url);
      }
      throw e;
    } catch (IOException e) {
      // Only the parse throws this now; the transport's own failures arrive unchecked.
      throw new UncheckedIOException(e);
    }
  }

  private <T> Optional<T> exchange(String url, Class<T> clazz) throws IOException {
    HttpRequest request = HttpRequest.newBuilder().setUrl(url).setTimeout(timeout).build();

    HttpResponse response = httpClient.execute(request);

    if (response.getStatusCode() == 404) {
      // Nothing to drain: the shared client reads the complete response before returning, so the
      // connection is already back in the pool by the time we look at the status.
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
}
