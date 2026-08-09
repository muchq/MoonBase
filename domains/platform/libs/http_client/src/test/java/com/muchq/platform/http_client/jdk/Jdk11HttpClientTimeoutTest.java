package com.muchq.platform.http_client.jdk;

import static org.assertj.core.api.Assertions.assertThat;
import static org.assertj.core.api.Assertions.assertThatThrownBy;

import com.muchq.platform.http_client.core.HttpRequest;
import java.io.IOException;
import java.io.UncheckedIOException;
import java.net.http.HttpTimeoutException;
import java.time.Duration;
import java.util.concurrent.TimeUnit;
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
   * Expiring the caller's wait is only half of a timeout. The exchange has to be torn down too.
   *
   * <p>Nothing above this line can tell the difference: a client that abandons the wait and leaves
   * the exchange running throws the same exception at the same moment, and every assertion in this
   * file passed against exactly that. What is left behind is a live subscriber on a live socket,
   * buffering whatever the peer sends, plus a {@code close()} that waits on an operation the caller
   * already gave up on.
   *
   * <p>So the peer is asked instead, and it is kept open past the assertion on purpose — closing it
   * would produce the disconnection this test is trying to observe.
   *
   * <p>The mechanism this pins is narrow: {@code cancel(true)} is what reaches the far end, and it
   * only does so on a future that has not completed. Timing the {@code get} keeps it that way, and
   * {@code orTimeout} did not — it completes the future itself, leaving a cancel that returns false
   * and touches nothing.
   */
  @Test
  @Timeout(60)
  public void aTimedOutExchangeIsTornDownAtThePeer() throws Exception {
    try (StalledServer server = new StalledServer(StalledServer.Behaviour.HEAD_THEN_STALL)) {
      assertThatThrownBy(() -> client().execute(request(server.url(), Duration.ofMillis(250))))
          .isInstanceOf(UncheckedIOException.class);

      assertThat(server.clientHungUp().await(30, TimeUnit.SECONDS))
          .as("the peer still sees a connected client, so the exchange outlived the deadline")
          .isTrue();
    }
  }

  /**
   * A deadline is not a cancellation, and one_d4's IndexWorker reads the difference off the
   * interrupt status rather than off an exception type. If an expiring timeout left that status
   * set, an ordinary slow upstream would be indistinguishable from a worker being told to let go:
   * the run would be abandoned instead of recorded as a failed attempt, and the flag would ride
   * back to a pooled thread that nobody interrupted.
   *
   * <p>Both stalls, because they leave through different catch blocks.
   */
  @Test
  @Timeout(60)
  public void aTimeoutLeavesTheInterruptStatusClear() throws Exception {
    Thread.interrupted(); // Stand on nothing a previous case left behind.
    for (StalledServer.Behaviour behaviour :
        new StalledServer.Behaviour[] {
          StalledServer.Behaviour.SILENT, StalledServer.Behaviour.HEAD_THEN_STALL
        }) {
      try (StalledServer server = new StalledServer(behaviour)) {
        assertThatThrownBy(() -> client().execute(request(server.url(), Duration.ofMillis(250))))
            .isInstanceOf(UncheckedIOException.class);

        assertThat(Thread.currentThread().isInterrupted())
            .as("a timeout against a %s peer must not look like a cancellation", behaviour)
            .isFalse();
      }
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
