package com.muchq.games.mcpserver.tools;

import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.ObjectMapper;
import com.muchq.games.one_d4.api.dto.AggregateRequest;
import com.muchq.games.one_d4.api.dto.AggregateResponse;
import com.muchq.games.one_d4.api.dto.AnalyzeRequest;
import com.muchq.games.one_d4.api.dto.AnalyzeResponse;
import com.muchq.games.one_d4.api.dto.IndexRequest;
import com.muchq.games.one_d4.api.dto.IndexResponse;
import com.muchq.games.one_d4.api.dto.QueryRequest;
import com.muchq.games.one_d4.api.dto.QueryResponse;
import com.muchq.platform.http_client.core.HttpClient;
import com.muchq.platform.http_client.core.HttpRequest;
import com.muchq.platform.http_client.core.HttpResponse;
import java.io.UncheckedIOException;
import java.net.URI;
import java.net.URISyntaxException;
import java.net.http.HttpTimeoutException;
import java.time.Duration;
import java.util.Optional;
import java.util.UUID;

/**
 * HTTP client for the one_d4 API — the corpus lives behind that service, and this server reaches it
 * the way any other client would.
 *
 * <p>Deliberately not a database client. one_d4 owns validation, the indexing lifecycle, retention,
 * query limits, the schema and its migrations; a second process with a connection string owns a
 * copy of all of that by accident, and the copies drift.
 *
 * <p>Request and response types are one_d4's own DTOs rather than parallel definitions here. A
 * hand-written mirror of a wire contract is a second place for it to be wrong, and nothing would
 * fail at the moment it diverged.
 */
public class OneD4Client {

  /**
   * Deadline for one call to one_d4, covering the whole exchange.
   *
   * <p>Not optional in practice: without it a peer that accepts the connection and then stalls
   * parks the calling thread forever, and forever outlasts every budget above it — including
   * IndexerFacade's polling ceiling, which is only consulted between calls and cannot interrupt one
   * already in flight. Comfortably above one_d4's own analyze ceiling so a slow-but-working request
   * is not cut off by the client.
   */
  static final Duration DEFAULT_TIMEOUT = Duration.ofSeconds(30);

  private final HttpClient httpClient;
  private final ObjectMapper mapper;
  private final String baseUrl;
  private final Duration timeout;

  public OneD4Client(HttpClient httpClient, ObjectMapper mapper, String baseUrl) {
    this(httpClient, mapper, baseUrl, DEFAULT_TIMEOUT);
  }

  OneD4Client(HttpClient httpClient, ObjectMapper mapper, String baseUrl, Duration timeout) {
    this.timeout = timeout;
    this.httpClient = httpClient;
    this.mapper = mapper;
    // Trailing slashes would produce //v1/index, which some routers treat as a different path.
    this.baseUrl =
        requireUsableAddress(
            baseUrl.endsWith("/") ? baseUrl.substring(0, baseUrl.length() - 1) : baseUrl);
  }

  /**
   * Rejects an upstream address this client could never call, at construction rather than on the
   * first request.
   *
   * <p>Written after a deployment spent its life pointed at {@code http://one_d4:8080}. Docker's
   * embedded DNS resolves that name and every other container on the network reached it; {@link
   * URI} applies RFC 3986's reg-name rule, hands back a null host for an authority containing an
   * underscore, and {@code java.net.http.HttpRequest} then refuses the URI outright — so mcpserver
   * never opened a socket. {@link #send} catches that {@link IllegalArgumentException} along with
   * the genuine transport failures and reports "not reachable", which is a statement about a
   * service that was healthy the whole time.
   *
   * <p>Failing here makes it a startup failure instead: the container never reports healthy, the
   * compose gate holds, and the message names the string to fix. The alternative — staying up and
   * serving the chess.com tools while every corpus tool errors — is the state this bug already
   * produced, and it took a packet capture's worth of digging to tell apart from an outage.
   */
  private static String requireUsableAddress(String baseUrl) {
    URI uri;
    try {
      uri = new URI(baseUrl);
    } catch (URISyntaxException e) {
      throw new IllegalArgumentException("one_d4 base URL " + baseUrl + " is not a valid URI", e);
    }
    if (uri.getHost() == null) {
      throw new IllegalArgumentException(
          "one_d4 base URL " + baseUrl + " has no host java.net.URI can parse");
    }
    return baseUrl;
  }

  public String baseUrl() {
    return baseUrl;
  }

  public IndexResponse index(IndexRequest request) {
    return post("/v1/index", request, IndexResponse.class);
  }

  /** Empty when one_d4 has no such request, which is a 404 rather than a failure. */
  public Optional<IndexResponse> status(UUID requestId) {
    String description = "GET /v1/index/" + requestId;
    HttpResponse response = send(get("/v1/index/" + requestId), description);
    if (response.getStatusCode() == 404) {
      return Optional.empty();
    }
    throwIfNotOk(response, description);
    return Optional.of(read(response, IndexResponse.class, description));
  }

  public QueryResponse query(QueryRequest request) {
    return post("/v1/query", request, QueryResponse.class);
  }

  public AggregateResponse aggregate(AggregateRequest request) {
    return post("/v1/aggregate", request, AggregateResponse.class);
  }

  public AnalyzeResponse analyze(AnalyzeRequest request) {
    return post("/v1/analyze", request, AnalyzeResponse.class);
  }

  private <T> T post(String path, Object body, Class<T> type) {
    String description = "POST " + path;
    HttpRequest request;
    try {
      request =
          HttpRequest.newBuilder()
              .setUrl(baseUrl + path)
              .setMethod(HttpRequest.Method.POST)
              .setContentType(HttpRequest.ContentType.JSON)
              .setAccept(HttpRequest.ContentType.JSON)
              .setTimeout(timeout)
              .setBody(mapper.writeValueAsString(body))
              .build();
    } catch (Exception e) {
      throw new UpstreamException(description + ": could not serialize request", e);
    }
    HttpResponse response = send(request, description);
    throwIfNotOk(response, description);
    return read(response, type, description);
  }

  private HttpRequest get(String path) {
    return HttpRequest.newBuilder()
        .setUrl(baseUrl + path)
        .setMethod(HttpRequest.Method.GET)
        .setAccept(HttpRequest.ContentType.JSON)
        .setTimeout(timeout)
        .build();
  }

  /**
   * The deadline lives on the request, and the shared client applies it to the whole exchange —
   * send, head and body. This used to wrap a Failsafe {@code Timeout} around the call as well,
   * because the transport's deadline stopped at the response head (#1336); with that fixed, a
   * second timer would only race the first and hand callers whichever exception won.
   */
  private HttpResponse send(HttpRequest request, String description) {
    try {
      return httpClient.execute(request);
    } catch (UncheckedIOException e) {
      if (e.getCause() instanceof HttpTimeoutException) {
        throw new UpstreamException(
            description + ": one_d4 at " + baseUrl + " did not answer within " + timeout, e);
      }
      throw notReachable(e);
    } catch (RuntimeException e) {
      throw notReachable(e);
    }
  }

  /**
   * Connection refused, DNS failure, a reset mid-body. one_d4 is not answering — distinct from a
   * 4xx, because nothing the caller changes about their arguments will fix it.
   */
  private UpstreamException notReachable(RuntimeException cause) {
    // The cause rode along on the exception and nothing ever printed it, so the only text anyone
    // read was a guess about the network. Naming it costs a few characters in the tool's error.
    Throwable root = cause.getCause() != null ? cause.getCause() : cause;
    String detail =
        root.getMessage() == null
            ? root.getClass().getSimpleName()
            : root.getClass().getSimpleName() + ": " + root.getMessage();
    return new UpstreamException(
        "one_d4 at " + baseUrl + " is not reachable (" + detail + ")", cause);
  }

  /**
   * 4xx becomes {@link IllegalArgumentException} carrying one_d4's own message, which is what the
   * tools already catch and report — so a rejected ChessQL query reads the same to an MCP client as
   * it did when the compiler ran in this process. Everything else is an upstream failure.
   */
  private void throwIfNotOk(HttpResponse response, String description) {
    int status = response.getStatusCode();
    if (status >= 200 && status < 300) {
      return;
    }
    if (status >= 400 && status < 500) {
      throw new IllegalArgumentException(
          errorMessage(response, description + " returned " + status));
    }
    throw new UpstreamException(
        description + " returned " + status + ": " + errorMessage(response, "no detail"));
  }

  /**
   * one_d4's ErrorHandler answers {@code {"error": "..."}}. Falls back to the supplied default when
   * the body is not that shape, so a proxy's HTML error page does not surface as a parse failure
   * that hides the status code.
   */
  private String errorMessage(HttpResponse response, String fallback) {
    try {
      JsonNode body = mapper.readTree(response.getAsString());
      JsonNode error = body.get("error");
      if (error != null && !error.isNull()) {
        return error.asText();
      }
    } catch (RuntimeException | java.io.IOException e) {
      // Fall through to the default.
    }
    return fallback;
  }

  private <T> T read(HttpResponse response, Class<T> type, String description) {
    try {
      return mapper.readValue(response.getAsString(), type);
    } catch (Exception e) {
      throw new UpstreamException(description + ": unreadable response from one_d4", e);
    }
  }

  /**
   * one_d4 could not answer: unreachable, a 5xx, or a body this client cannot parse. Separate from
   * {@link IllegalArgumentException} because the two want different words in front of a model — one
   * is "fix your arguments", the other is "the corpus service is down, try later".
   */
  public static class UpstreamException extends RuntimeException {
    public UpstreamException(String message) {
      super(message);
    }

    public UpstreamException(String message, Throwable cause) {
      super(message, cause);
    }
  }
}
