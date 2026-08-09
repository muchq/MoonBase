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
   * The deadline covers time-to-headers, and this pins the boundary: a peer that sends a complete
   * head and then stalls mid-body gets past {@code execute} without expiring, because the JDK's
   * request timeout ends when the response head arrives and the body is still being streamed.
   *
   * <p>So the body read after this point is <em>not</em> bounded by the timeout. Closing that gap
   * means either buffering the whole body inside the deadline (which would change ChessClient's
   * memory profile on a month of PGNs) or a separate read deadline — a decision about this shared
   * client rather than about any one caller. Recorded here rather than left as folklore: this test
   * is what tells the next reader which half is covered.
   */
  @Test
  @Timeout(60)
  public void theDeadlineCoversHeadersAndNotTheBodyThatFollows() throws Exception {
    try (StalledServer server = new StalledServer(StalledServer.Behaviour.HEAD_THEN_STALL)) {
      // Returns rather than throwing: the head arrived inside the deadline.
      var response = client().execute(request(server.url(), Duration.ofMillis(250)));

      assertThat(response.getStatusCode())
          .as("the head arrived, so execute completes and the stall moves to the body read")
          .isEqualTo(200);
    }
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

    assertThat(noTimeout.getTimeout()).isEmpty();
    assertThat(request("http://example.invalid/x", Duration.ofSeconds(3)).getTimeout())
        .contains(Duration.ofSeconds(3));
  }
}
