package com.muchq.platform.http_client.jdk;

import static org.assertj.core.api.Assertions.assertThat;
import static org.assertj.core.api.Assertions.assertThatThrownBy;

import com.muchq.platform.http_client.core.HttpRequest;
import java.io.UncheckedIOException;
import java.net.http.HttpTimeoutException;
import java.time.Duration;
import org.junit.jupiter.api.Test;
import org.junit.jupiter.api.Timeout;

/**
 * A request timeout, against a server that genuinely stalls.
 *
 * <p>Both places a caller can be parked are covered, because they fail differently: a peer that
 * accepts and never answers leaves the thread in {@code send}, while one that sends a head and then
 * goes quiet leaves it in the body read, having already seen a 200. A timeout that only covered the
 * first would look correct in a test and hang in production against the second.
 *
 * <p>The {@code @Timeout} on each case is the backstop that makes a failure a failure: without a
 * working deadline these do not fail, they hang, and a hung test in CI reads as an infrastructure
 * problem rather than a bug.
 */
public class Jdk11HttpClientTimeoutTest {

  private static Jdk11HttpClient client() {
    return new Jdk11HttpClient(java.net.http.HttpClient.newHttpClient());
  }

  private static HttpRequest request(String url, Duration timeout) {
    return HttpRequest.newBuilder()
        .setUrl(url)
        .setMethod(HttpRequest.Method.GET)
        .setResponseHeadersTimeout(timeout)
        .build();
  }

  @Test
  @Timeout(60)
  public void aPeerThatNeverAnswersExpiresTheDeadline() throws Exception {
    try (StalledServer server = new StalledServer(StalledServer.Behaviour.SILENT)) {
      assertThatThrownBy(() -> client().execute(request(server.url(), Duration.ofMillis(250))))
          .isInstanceOf(UncheckedIOException.class)
          .hasCauseInstanceOf(HttpTimeoutException.class);
    }
  }

  /**
   * The headers deadline stops at the headers, which is why the body one exists.
   *
   * <p>A peer that sends a complete head and then stalls gets past {@code execute} without
   * expiring: the JDK's request timeout ends when the response head arrives. Pinned so the division
   * of labour between the two settings is written down rather than inferred.
   */
  @Test
  @Timeout(60)
  public void theHeadersDeadlineStopsAtTheHeaders() throws Exception {
    try (StalledServer server = new StalledServer(StalledServer.Behaviour.HEAD_THEN_STALL)) {
      var response = client().execute(request(server.url(), Duration.ofMillis(250)));

      assertThat(response.getStatusCode())
          .as("the head arrived, so execute completes and the stall moves to the body read")
          .isEqualTo(200);
    }
  }

  /**
   * The gap that mattered: a peer that answers and then stalls mid-body must not park the caller.
   *
   * <p>This is the case that costs an indexing worker its request lease, because the thread stuck
   * here is holding one. Before the body deadline existed there was no ceiling on it at all — the
   * headers deadline is already satisfied by the time the stall starts.
   *
   * <p>{@code @Timeout} is the backstop that makes a regression a failure rather than a hung CI
   * job: without a working deadline this does not fail, it hangs.
   */
  @Test
  @Timeout(60)
  public void aStalledBodyExpiresOnTheBodyDeadline() throws Exception {
    try (StalledServer server = new StalledServer(StalledServer.Behaviour.HEAD_THEN_STALL)) {
      HttpRequest bounded =
          HttpRequest.newBuilder()
              .setUrl(server.url())
              .setMethod(HttpRequest.Method.GET)
              .setBodyReadTimeout(Duration.ofMillis(250))
              .build();

      var response = client().execute(bounded);

      assertThatThrownBy(response::getAsBytes)
          .as("an unbounded body read is how a stalled peer parks a worker holding a lease")
          .hasRootCauseInstanceOf(HttpTimeoutException.class);
    }
  }

  /**
   * A body that arrives in full inside the deadline reads normally — the wrapper must not turn a
   * slow-but-working response into a failure, and must not truncate it.
   */
  @Test
  @Timeout(60)
  public void aBodyThatArrivesInTimeIsReadWhole() throws Exception {
    try (StalledServer server = new StalledServer(StalledServer.Behaviour.HEAD_THEN_CLOSE)) {
      HttpRequest bounded =
          HttpRequest.newBuilder()
              .setUrl(server.url())
              .setMethod(HttpRequest.Method.GET)
              .setBodyReadTimeout(Duration.ofSeconds(30))
              .build();

      var response = client().execute(bounded);

      // HEAD_THEN_CLOSE hangs up short of its declared length: a real failure, and it must stay
      // one rather than being reported as a timeout.
      assertThatThrownBy(response::getAsBytes)
          .as("a truncated body is not a timeout")
          .isNotInstanceOf(HttpTimeoutException.class);
    }
  }

  /** No body deadline set keeps the previous behaviour: the stream is handed over unwrapped. */
  @Test
  @Timeout(60)
  public void aRequestWithoutABodyDeadlineCarriesNone() {
    assertThat(
            HttpRequest.newBuilder()
                .setUrl("http://example.invalid/x")
                .setMethod(HttpRequest.Method.GET)
                .build()
                .getBodyReadTimeout())
        .isEmpty();
    assertThat(
            HttpRequest.newBuilder()
                .setUrl("http://example.invalid/x")
                .setMethod(HttpRequest.Method.GET)
                .setTimeouts(Duration.ofSeconds(5))
                .build()
                .getBodyReadTimeout())
        .as("setTimeouts sets both halves, which is the common case")
        .contains(Duration.ofSeconds(5));
  }

  /**
   * No timeout set keeps the previous behaviour, so this stays an opt-in for callers that want it.
   */
  @Test
  @Timeout(60)
  public void aRequestWithoutATimeoutCarriesNone() {
    HttpRequest noTimeout =
        HttpRequest.newBuilder()
            .setUrl("http://example.invalid/x")
            .setMethod(HttpRequest.Method.GET)
            .build();

    assertThat(noTimeout.getResponseHeadersTimeout()).isEmpty();
    assertThat(
            request("http://example.invalid/x", Duration.ofSeconds(3)).getResponseHeadersTimeout())
        .contains(Duration.ofSeconds(3));
  }
}
