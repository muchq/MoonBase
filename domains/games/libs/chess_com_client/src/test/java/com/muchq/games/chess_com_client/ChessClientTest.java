package com.muchq.games.chess_com_client;

import static org.assertj.core.api.Assertions.assertThat;

import com.fasterxml.jackson.databind.ObjectMapper;
import com.muchq.platform.http_client.core.HttpClient;
import com.muchq.platform.http_client.core.HttpRequest;
import com.muchq.platform.http_client.core.HttpResponse;
import java.io.ByteArrayInputStream;
import java.io.InputStream;
import java.nio.charset.StandardCharsets;
import java.time.YearMonth;
import java.util.Optional;
import org.junit.Test;

public class ChessClientTest {
  private static final ObjectMapper MAPPER = new ObjectMapper();

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
    assertThat(result.get().games().get(0).url())
        .isEqualTo("https://www.chess.com/game/live/12345");
  }

  @Test
  public void testFetchGames_notFound() {
    HttpClient httpClient = new StubHttpClient(404, "");
    ChessClient client = new ChessClient(httpClient, MAPPER);

    Optional<GamesResponse> result = client.fetchGames("hikaru", YearMonth.of(2024, 1));

    assertThat(result).isEmpty();
  }
}
