package com.muchq.platform.http_client.jdk;

import static com.muchq.platform.http_client.jdk.InterruptProbe.interruptWhileBlocked;
import static com.muchq.platform.http_client.jdk.InterruptProbe.newClient;
import static org.assertj.core.api.Assertions.assertThat;
import static org.assertj.core.api.Assertions.assertThatThrownBy;

import com.muchq.platform.http_client.core.HttpClient;
import com.muchq.platform.http_client.core.HttpRequest;
import com.muchq.platform.http_client.core.InterruptedRequestException;
import java.io.UncheckedIOException;
import org.junit.jupiter.api.BeforeEach;
import org.junit.jupiter.api.Test;
import org.junit.jupiter.api.Timeout;

/**
 * Whether a caller can stop waiting.
 *
 * <p>An HTTP call is unbounded here by construction: no request timeout is configured, and a peer
 * that accepts a connection and then goes quiet produces no error to unblock on. Whatever thread
 * made the call is parked until the peer says something, which it may never do. {@link
 * Thread#interrupt()} is the only lever left, and it is worth having only if two things hold — the
 * call actually comes back, and the caller can tell it came back because it was told to stop rather
 * than because the transport broke. The second is the one that is easy to get wrong and impossible
 * to recover further up: report an interrupt as a generic {@code RuntimeException} and the caller
 * has to treat its own cancellation as a failure of whatever it was fetching.
 *
 * <p>Real sockets throughout. A stub cannot block, and blocking is the entire subject.
 */
public class Jdk11HttpClientInterruptTest {

  /**
   * One test here sets the interrupt status on the thread running it, and JUnit hands that same
   * thread to the next one. Clearing it up front keeps each case standing on its own, rather than
   * passing or failing on what its predecessor left behind.
   */
  @BeforeEach
  public void clearAnyLeakedInterrupt() {
    Thread.interrupted();
  }

  /**
   * The headline: a thread parked in {@code execute} against a peer that will never answer comes
   * back when it is interrupted, and says so.
   */
  @Test
  @Timeout(60)
  public void interruptingAWaitForAResponseAbortsTheCall() throws Exception {
    // Declared before the server so it is closed after it: shutting the peer's sockets first is
    // what guarantees this teardown cannot itself hang on the exchange the test just abandoned.
    try (HttpClient client = newClient();
        StalledServer server = new StalledServer(StalledServer.Behaviour.SILENT)) {
      InterruptProbe.Outcome outcome =
          interruptWhileBlocked(server.connected(), () -> client.execute(get(server)));

      assertThat(outcome.thrown())
          .as(
              "an interrupted call must report the interrupt as one — anything else and the caller"
                  + " cannot tell its own cancellation from a broken connection")
          .isInstanceOf(InterruptedRequestException.class);
      assertThat(outcome.interruptStatusSet())
          .as(
              "and must leave the interrupt status set, or the next blocking call the caller makes"
                  + " waits out its own full timeout before noticing")
          .isTrue();
    }
  }

  /**
   * The negative that stops the test above from passing for the wrong reason. A refused connection
   * is a failure, not a cancellation, and it has to stay one — a client that reported every
   * transport error as an interrupt would pass the interrupt test perfectly.
   */
  @Test
  @Timeout(60)
  public void anOrdinaryConnectionFailureIsNotReportedAsAnInterrupt() throws Exception {
    try (HttpClient client = newClient()) {
      HttpRequest request = HttpRequest.newBuilder().setUrl(StalledServer.unusedPortUrl()).build();

      assertThatThrownBy(() -> client.execute(request))
          .isInstanceOf(UncheckedIOException.class)
          .isNotInstanceOf(InterruptedRequestException.class);
      assertThat(Thread.currentThread().isInterrupted())
          .as("a failed request must not leave the caller's thread looking interrupted")
          .isFalse();
    }
  }

  /**
   * A thread that is already interrupted does not get parked in the first place.
   *
   * <p>This is what matters to a caller unwinding through a loop of calls: having been told to
   * stop, it must not sit through one more unbounded wait per remaining iteration. The request is
   * made against the same peer that never answers, so a call that did wait would not return at all
   * and this would time out rather than pass.
   */
  @Test
  @Timeout(60)
  public void aCallOnAnAlreadyInterruptedThreadDoesNotWaitForTheServer() throws Exception {
    try (HttpClient client = newClient();
        StalledServer server = new StalledServer(StalledServer.Behaviour.SILENT)) {
      HttpRequest request = get(server);
      Thread.currentThread().interrupt();

      assertThatThrownBy(() -> client.execute(request))
          .isInstanceOf(InterruptedRequestException.class);
      assertThat(Thread.interrupted())
          .as("the status is restored on the way out, not consumed by the call")
          .isTrue();
    }
  }

  // --- wiring ---------------------------------------------------------------------------------

  private static HttpRequest get(StalledServer server) {
    return HttpRequest.newBuilder().setUrl(server.url()).build();
  }
}
