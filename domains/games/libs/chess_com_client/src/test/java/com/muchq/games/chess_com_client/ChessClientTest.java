package com.muchq.games.chess_com_client;

import static org.assertj.core.api.Assertions.assertThat;
import static org.assertj.core.api.Assertions.assertThatThrownBy;

import com.fasterxml.jackson.databind.ObjectMapper;
import com.muchq.platform.http_client.core.HttpClient;
import com.muchq.platform.http_client.core.HttpRequest;
import com.muchq.platform.http_client.core.HttpResponse;
import com.muchq.platform.json.JsonUtils;
import java.io.ByteArrayInputStream;
import java.io.InputStream;
import java.nio.charset.StandardCharsets;
import java.time.Instant;
import java.time.YearMonth;
import java.util.Optional;
import org.junit.jupiter.api.Test;

public class ChessClientTest {

  /**
   * The mapper production injects, not a locally built one. Both {@code McpModule} and {@code
   * IndexerModule} hand ChessClient {@code JsonUtils.mapper()}, which carries JavaTimeModule and
   * FAIL_ON_UNKNOWN_PROPERTIES=false; a bare {@code new ObjectMapper()} has neither, so it would
   * reject payloads the services read and accept shapes they do not.
   */
  private static final ObjectMapper MAPPER = JsonUtils.mapper();

  private static class StubHttpClient implements HttpClient {
    private final int statusCode;
    private final String responseBody;

    public StubHttpClient(int statusCode, String responseBody) {
      this.statusCode = statusCode;
      this.responseBody = responseBody;
    }

    @Override
    public HttpResponse execute(HttpRequest request) {
      return new StubHttpResponse(statusCode, responseBody);
    }

    @Override
    public HttpResponse executeAsync(HttpRequest request) {
      return execute(request);
    }

    @Override
    public void close() {}
  }

  private static class StubHttpResponse implements HttpResponse {
    private final int statusCode;
    private final String responseBody;

    public StubHttpResponse(int statusCode, String responseBody) {
      this.statusCode = statusCode;
      this.responseBody = responseBody;
    }

    @Override
    public HttpRequest getRequest() {
      return null;
    }

    @Override
    public int getStatusCode() {
      return statusCode;
    }

    @Override
    public boolean isSuccess() {
      return statusCode >= 200 && statusCode < 300;
    }

    @Override
    public boolean isError() {
      return statusCode >= 400;
    }

    @Override
    public boolean isClientError() {
      return statusCode >= 400 && statusCode < 500;
    }

    @Override
    public boolean isServerError() {
      return statusCode >= 500;
    }

    @Override
    public java.util.List<com.muchq.platform.http_client.core.Header> getHeaders() {
      return java.util.Collections.emptyList();
    }

    @Override
    public String getAsString() {
      return responseBody;
    }

    @Override
    public byte[] getAsBytes() {
      return responseBody.getBytes(StandardCharsets.UTF_8);
    }

    @Override
    public InputStream getAsInputStream() {
      return new ByteArrayInputStream(responseBody.getBytes(StandardCharsets.UTF_8));
    }
  }

  @Test
  public void testFetchPlayer_normalizesUsernameToLowercase() {
    CapturingHttpClient httpClient = new CapturingHttpClient(404, "");
    ChessClient client = new ChessClient(httpClient, MAPPER);

    client.fetchPlayer("Hikaru");

    assertThat(httpClient.lastUrl).contains("/hikaru");
    assertThat(httpClient.lastUrl).doesNotContain("/Hikaru");
  }

  @Test
  public void testFetchStats_normalizesUsernameToLowercase() {
    CapturingHttpClient httpClient = new CapturingHttpClient(404, "");
    ChessClient client = new ChessClient(httpClient, MAPPER);

    client.fetchStats("Hikaru");

    assertThat(httpClient.lastUrl).contains("/hikaru/stats");
    assertThat(httpClient.lastUrl).doesNotContain("/Hikaru");
  }

  @Test
  public void testFetchGames_normalizesUsernameToLowercase() {
    CapturingHttpClient httpClient = new CapturingHttpClient(404, "");
    ChessClient client = new ChessClient(httpClient, MAPPER);

    client.fetchGames("Hikaru", YearMonth.of(2024, 1));

    assertThat(httpClient.lastUrl).contains("/hikaru/games/");
    assertThat(httpClient.lastUrl).doesNotContain("/Hikaru");
  }

  @Test
  public void testFetchPlayer_success() {
    String playerJson =
        """
        {
          "player_id": 12345,
          "url": "https://www.chess.com/member/hikaru",
          "username": "hikaru",
          "followers": 1000,
          "country": "https://api.chess.com/pub/country/US",
          "last_online": 1234567890,
          "joined": 1234567890,
          "status": "premium",
          "is_streamer": true
        }
        """;

    HttpClient httpClient = new StubHttpClient(200, playerJson);
    ChessClient client = new ChessClient(httpClient, MAPPER);

    Optional<Player> result = client.fetchPlayer("hikaru");

    assertThat(result).isPresent();
    assertThat(result.get().username()).isEqualTo("hikaru");
    assertThat(result.get().streamer()).isTrue();
  }

  /**
   * chess.com's field names and this record's component names disagree on six fields, and the
   * {@code @JsonCreator} factory's {@code @JsonProperty} annotations are the entire bridge. Nothing
   * else would notice them breaking: a name that stops binding does not throw, it yields null (or
   * 0), so a wrong answer reaches the caller looking like a player with no profile URL who has
   * never been online.
   *
   * <p>{@code @id} is the one worth naming. It is not a legal Java identifier, so it can only ever
   * arrive through the annotation — there is no field-name or getter inference to fall back on —
   * and it is what {@code chess_com_player} puts on the wire as {@code playerApiUrl}.
   *
   * <p>Asserts every renamed component rather than a sample. Checking two of six leaves four
   * renames that can break without failing anything, which is exactly how {@code @id} stayed
   * uncovered until a mutation check went looking for it.
   */
  @Test
  public void testFetchPlayer_bindsChessComsWireNamesOntoRenamedComponents() {
    String playerJson =
        """
        {
          "player_id": 12345,
          "@id": "https://api.chess.com/pub/player/hikaru",
          "url": "https://www.chess.com/member/hikaru",
          "username": "hikaru",
          "country": "https://api.chess.com/pub/country/US",
          "last_online": 1234567890,
          "joined": 1200000000,
          "status": "premium",
          "is_streamer": true
        }
        """;

    ChessClient client = new ChessClient(new StubHttpClient(200, playerJson), MAPPER);

    Player player = client.fetchPlayer("hikaru").orElseThrow();

    assertThat(player.playerId()).as("player_id").isEqualTo(12345);
    assertThat(player.playerApiUrl())
        .as("@id, which no inference can supply")
        .isEqualTo("https://api.chess.com/pub/player/hikaru");
    assertThat(player.playerPageUrl())
        .as("url, distinct from @id and easy to swap with it")
        .isEqualTo("https://www.chess.com/member/hikaru");
    assertThat(player.countryUrl()).as("country").isEqualTo("https://api.chess.com/pub/country/US");
    assertThat(player.lastOnlineAt())
        .as("last_online")
        .isEqualTo(Instant.ofEpochSecond(1234567890));
    assertThat(player.joinedAt()).as("joined").isEqualTo(Instant.ofEpochSecond(1200000000));
    assertThat(player.streamer()).as("is_streamer").isTrue();
  }

  @Test
  public void testFetchPlayer_mapsTitleLocationAndFide() {
    String playerJson =
        """
        {
          "player_id": 41,
          "url": "https://www.chess.com/member/rpragchess",
          "username": "rpragchess",
          "title": "GM",
          "location": "Chennai",
          "fide": 2758,
          "last_online": 1234567890,
          "joined": 1234567890,
          "status": "premium",
          "is_streamer": false
        }
        """;

    HttpClient httpClient = new StubHttpClient(200, playerJson);
    ChessClient client = new ChessClient(httpClient, MAPPER);

    Optional<Player> result = client.fetchPlayer("rpragchess");

    assertThat(result).isPresent();
    assertThat(result.get().title()).isEqualTo("GM");
    assertThat(result.get().location()).isEqualTo("Chennai");
    assertThat(result.get().fideRating()).isEqualTo(2758);
  }

  @Test
  public void testFetchPlayer_untitledPlayerHasNullTitleAndFide() {
    String playerJson =
        """
        {
          "player_id": 42,
          "url": "https://www.chess.com/member/someuser",
          "username": "someuser",
          "last_online": 1234567890,
          "joined": 1234567890,
          "status": "basic",
          "is_streamer": false
        }
        """;

    HttpClient httpClient = new StubHttpClient(200, playerJson);
    ChessClient client = new ChessClient(httpClient, MAPPER);

    Optional<Player> result = client.fetchPlayer("someuser");

    assertThat(result).isPresent();
    assertThat(result.get().title()).isNull();
    assertThat(result.get().location()).isNull();
    assertThat(result.get().fideRating()).isNull();
  }

  /**
   * A peer that answers and then stalls mid-body must expire rather than park the caller.
   *
   * <p>The worst case in this repo, and the reason the deadline is here rather than left to the
   * shared client: the caller is an indexing worker holding a request lease. A thread parked
   * forever keeps that row owned by a process that is never coming back, so the request burns an
   * attempt and the lease has to expire before anyone else can pick it up (#1336).
   *
   * <p>Against a real socket, because the claim is about a thread that would otherwise be parked on
   * one. The deadline itself is the shared client's now; what this pins is that a stalled body
   * still reaches a caller of <em>this</em> class as the documented 504.
   *
   * <p>{@code @Timeout} is the backstop that makes a regression a failure rather than a hung CI
   * job: without a working deadline this does not fail, it hangs.
   *
   * <p>The client is aimed at that socket through the package-private base URL. It used to be built
   * with the public constructor, which pins chess.com — so this stood up a stalling server, never
   * connected to it, and called the live API instead. It then passed whenever chess.com took longer
   * than the 300ms deadline and failed when it did not, which on a fast link is most of the time:
   * {@code /pub/player/stalled/games/2024/01} is a real archive that answers 200 in about a tenth
   * of a second. Green or red, it never once exercised a stalled body.
   */
  @Test
  @org.junit.jupiter.api.Timeout(60)
  public void aStalledBodyExpiresRatherThanParkingTheCaller() throws Exception {
    java.util.concurrent.CountDownLatch connected = new java.util.concurrent.CountDownLatch(1);
    try (java.net.ServerSocket socket =
        new java.net.ServerSocket(0, 8, java.net.InetAddress.getLoopbackAddress())) {
      Thread server =
          new Thread(
              () -> {
                try (java.net.Socket accepted = socket.accept()) {
                  connected.countDown();
                  // A head promising 4096 bytes, ten delivered, then silence.
                  accepted
                      .getOutputStream()
                      .write(
                          ("HTTP/1.1 200 OK\r\nContent-Type: application/json\r\n"
                                  + "Content-Length: 4096\r\n\r\n{\"games\":[")
                              .getBytes(StandardCharsets.UTF_8));
                  accepted.getOutputStream().flush();
                  Thread.sleep(3_000);
                } catch (Exception e) {
                  Thread.currentThread().interrupt();
                }
              });
      server.setDaemon(true);
      server.start();

      ChessClient client =
          new ChessClient(
              new com.muchq.platform.http_client.jdk.Jdk11HttpClient(
                  java.net.http.HttpClient.newHttpClient()),
              MAPPER,
              java.time.Duration.ofMillis(300),
              "http://127.0.0.1:" + socket.getLocalPort() + "/pub/player");

      Thread.interrupted(); // Stand on nothing an earlier case left behind.

      assertThatThrownBy(() -> client.fetchGames("stalled", YearMonth.of(2024, 1)))
          .isInstanceOf(ChessComApiException.class)
          .hasMessageContaining("did not answer within")
          .satisfies(e -> assertThat(((ChessComApiException) e).statusCode()).isEqualTo(504));

      assertThat(connected.await(1, java.util.concurrent.TimeUnit.SECONDS))
          .as(
              "the stalling server is the peer under test; if nothing connected to it, the 504"
                  + " above came from somewhere else and this case proves nothing")
          .isTrue();

      assertThat(Thread.currentThread().isInterrupted())
          .as(
              "a slow upstream is not a cancellation; IndexWorker reads this status to decide"
                  + " whether to abandon a request or record a failed attempt against it")
          .isFalse();
    }
  }

  /**
   * The same translation, without a socket, so it holds whichever timer fired.
   *
   * <p>The socket test above cannot distinguish these: it produces one timeout and cannot say what
   * would happen to the other. There were two while this class ran its own Failsafe deadline
   * alongside the transport's, and a caller got {@link ChessComApiException} or a bare {@code
   * UncheckedIOException} depending on which scheduler thread won a race between two equal
   * durations — reproducible in production, unreproducible in a test.
   *
   * <p>Now the shared client raises exactly this, and it is the only thing that raises it.
   */
  @Test
  public void aTransportTimeoutIsAlwaysReportedAsAGatewayTimeout() {
    HttpClient timingOut =
        new HttpClient() {
          @Override
          public HttpResponse execute(HttpRequest request) {
            throw new java.io.UncheckedIOException(
                new java.net.http.HttpTimeoutException("request did not complete within deadline"));
          }

          @Override
          public HttpResponse executeAsync(HttpRequest request) {
            return execute(request);
          }

          @Override
          public void close() {}
        };

    ChessClient client = new ChessClient(timingOut, MAPPER, java.time.Duration.ofSeconds(7));

    assertThatThrownBy(() -> client.fetchPlayer("hikaru"))
        .isInstanceOf(ChessComApiException.class)
        .satisfies(e -> assertThat(((ChessComApiException) e).statusCode()).isEqualTo(504))
        .hasMessageContaining("did not answer within PT7S");
  }

  /**
   * The negative half. Not every {@code UncheckedIOException} is a deadline — a reset connection is
   * one too — and turning those into a 504 would tell a caller to wait and retry a request that
   * failed outright.
   */
  @Test
  public void anOrdinaryTransportFailureIsNotDressedUpAsATimeout() {
    HttpClient reset =
        new HttpClient() {
          @Override
          public HttpResponse execute(HttpRequest request) {
            throw new java.io.UncheckedIOException(
                new java.io.IOException("connection reset by peer"));
          }

          @Override
          public HttpResponse executeAsync(HttpRequest request) {
            return execute(request);
          }

          @Override
          public void close() {}
        };

    ChessClient client = new ChessClient(reset, MAPPER, java.time.Duration.ofSeconds(7));

    assertThatThrownBy(() -> client.fetchPlayer("hikaru"))
        .isInstanceOf(java.io.UncheckedIOException.class)
        .hasMessageContaining("connection reset by peer");
  }

  @Test
  public void testFetchPlayer_rateLimitedThrowsWithStatusCode() {
    HttpClient httpClient = new StubHttpClient(429, "slow down");
    ChessClient client = new ChessClient(httpClient, MAPPER);

    assertThatThrownBy(() -> client.fetchPlayer("hikaru"))
        .isInstanceOf(ChessComApiException.class)
        .satisfies(e -> assertThat(((ChessComApiException) e).statusCode()).isEqualTo(429))
        .hasMessageContaining("429");
  }

  @Test
  public void testFetchGames_serverErrorThrowsWithStatusCode() {
    HttpClient httpClient = new StubHttpClient(503, "unavailable");
    ChessClient client = new ChessClient(httpClient, MAPPER);

    assertThatThrownBy(() -> client.fetchGames("hikaru", YearMonth.of(2024, 1)))
        .isInstanceOf(ChessComApiException.class)
        .satisfies(e -> assertThat(((ChessComApiException) e).statusCode()).isEqualTo(503));
  }

  @Test
  public void testFetchPlayer_notFound() {
    HttpClient httpClient = new StubHttpClient(404, "");
    ChessClient client = new ChessClient(httpClient, MAPPER);

    Optional<Player> result = client.fetchPlayer("nonexistent");

    assertThat(result).isEmpty();
  }

  @Test
  public void testFetchStats_success() {
    String statsJson =
        """
        {
          "chess_rapid": {
            "last": {
              "rating": 2800,
              "date": 1234567890,
              "rd": 50
            },
            "best": {
              "rating": 2850,
              "date": 1234567890,
              "game": "https://www.chess.com/game/live/12345"
            },
            "record": {
              "win": 1000,
              "loss": 200,
              "draw": 100
            }
          }
        }
        """;

    HttpClient httpClient = new StubHttpClient(200, statsJson);
    ChessClient client = new ChessClient(httpClient, MAPPER);

    Optional<StatsResponse> result = client.fetchStats("hikaru");

    assertThat(result).isPresent();
    assertThat(result.get().chessRapid()).isNotNull();
  }

  @Test
  public void testFetchGames_success() {
    String gamesJson =
        """
        {
          "games": [
            {
              "url": "https://www.chess.com/game/live/12345",
              "pgn": "[Event \\"Live Chess\\"]\\n1. e4 e5",
              "end_time": 1234567890,
              "rated": true,
              "tcn": "mCvSkBwRnBxE",
              "uuid": "abc123",
              "initial_setup": "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1",
              "fen": "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1",
              "time_class": "rapid",
              "rules": "chess",
              "white": {
                "rating": 2800,
                "result": "win",
                "username": "hikaru"
              },
              "black": {
                "rating": 2750,
                "result": "checkmated",
                "username": "opponent"
              }
            }
          ]
        }
        """;

    HttpClient httpClient = new StubHttpClient(200, gamesJson);
    ChessClient client = new ChessClient(httpClient, MAPPER);

    Optional<GamesResponse> result = client.fetchGames("hikaru", YearMonth.of(2024, 1));

    assertThat(result).isPresent();
    assertThat(result.get().games()).hasSize(1);
    PlayedGame game = result.get().games().get(0);
    assertThat(game.url()).isEqualTo("https://www.chess.com/game/live/12345");

    // The renamed fields, which asserting url alone left unproven. white/black are the load-bearing
    // pair: ChessComGamesTool reads whiteResult()/blackResult() to pick a side, and IndexWorker
    // reads their ratings and usernames into every indexed row. A rename that stops binding yields
    // null rather than throwing, so games come back with no players on them.
    assertThat(game.whiteResult().username()).as("white").isEqualTo("hikaru");
    assertThat(game.whiteResult().rating()).isEqualTo(2800);
    assertThat(game.whiteResult().result()).isEqualTo("win");
    assertThat(game.blackResult().username()).as("black").isEqualTo("opponent");
    assertThat(game.blackResult().result()).isEqualTo("checkmated");
    assertThat(game.endTime()).as("end_time").isEqualTo(Instant.ofEpochSecond(1234567890));
    assertThat(game.timeClass()).as("time_class").isEqualTo("rapid");
    assertThat(game.initialSetup())
        .as("initial_setup")
        .isEqualTo("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1");
    assertThat(game.rated()).isTrue();
    assertThat(game.tcn()).isEqualTo("mCvSkBwRnBxE");
  }

  @Test
  public void testFetchGames_notFound() {
    HttpClient httpClient = new StubHttpClient(404, "");
    ChessClient client = new ChessClient(httpClient, MAPPER);

    Optional<GamesResponse> result = client.fetchGames("hikaru", YearMonth.of(2024, 1));

    assertThat(result).isEmpty();
  }

  private static class CapturingHttpClient extends StubHttpClient {
    String lastUrl;

    public CapturingHttpClient(int statusCode, String responseBody) {
      super(statusCode, responseBody);
    }

    @Override
    public HttpResponse execute(HttpRequest request) {
      lastUrl = request.getUrl().toString();
      return super.execute(request);
    }
  }
}
