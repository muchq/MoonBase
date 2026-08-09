package com.muchq.platform.http_client.jdk;

import static org.assertj.core.api.Assertions.assertThat;
import static org.assertj.core.api.Assertions.assertThatThrownBy;

import com.muchq.platform.http_client.core.HttpRequest;
import java.io.IOException;
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
        .setTimeout(timeout)
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
   * The case the old deadline could not reach: a peer that sends a complete head and then stalls
   * mid-body.
   *
   * <p>Before the client sent asynchronously this got past {@code execute} without expiring — the
   * JDK's request timeout ends when the head arrives — and the caller parked in the body read with
   * no ceiling at all. Waiting on the whole response is what closes that.
   *
   * <p>This is the case that costs an indexing worker its request lease, since the thread stuck
   * here is holding one.
   */
  @Test
  @Timeout(60)
  public void aPeerThatStallsMidBodyExpiresToo() throws Exception {
    try (StalledServer server = new StalledServer(StalledServer.Behaviour.HEAD_THEN_STALL)) {
      assertThatThrownBy(() -> client().execute(request(server.url(), Duration.ofMillis(250))))
          .isInstanceOf(UncheckedIOException.class)
          .hasCauseInstanceOf(HttpTimeoutException.class);
    }
  }

  /**
   * A truncated body is a real failure and must stay one. The peer hangs up short of its declared
   * length rather than stalling, so reporting a timeout would send a caller to retry a wait that
   * never happened.
   */
  @Test
  @Timeout(60)
  public void aTruncatedBodyIsNotReportedAsATimeout() throws Exception {
    try (StalledServer server = new StalledServer(StalledServer.Behaviour.HEAD_THEN_CLOSE)) {
      assertThatThrownBy(() -> client().execute(request(server.url(), Duration.ofSeconds(30))))
          .as("a short body is a broken response, not a slow one")
          .isNotInstanceOf(HttpTimeoutException.class)
          .hasRootCauseInstanceOf(IOException.class);
    }
  }

  /** No deadline set keeps the previous behaviour, so this stays opt-in. */
  @Test
  @Timeout(60)
  public void aRequestWithoutATimeoutCarriesNone() {
    assertThat(
            HttpRequest.newBuilder()
                .setUrl("http://example.invalid/x")
                .setMethod(HttpRequest.Method.GET)
                .build()
                .getTimeout())
        .isEmpty();
    assertThat(request("http://example.invalid/x", Duration.ofSeconds(3)).getTimeout())
        .contains(Duration.ofSeconds(3));
  }
}
