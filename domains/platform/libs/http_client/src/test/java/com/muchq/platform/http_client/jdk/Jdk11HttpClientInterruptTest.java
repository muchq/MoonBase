package com.muchq.platform.http_client.jdk;

import static com.muchq.platform.http_client.jdk.InterruptProbe.interruptWhileBlocked;
import static com.muchq.platform.http_client.jdk.InterruptProbe.newClient;
import static org.assertj.core.api.Assertions.assertThat;
import static org.assertj.core.api.Assertions.assertThatThrownBy;

import com.muchq.platform.http_client.core.HttpClient;
import com.muchq.platform.http_client.core.HttpRequest;
import org.junit.jupiter.api.BeforeEach;
import org.junit.jupiter.api.Test;
import org.junit.jupiter.api.Timeout;

/**
 * What a caller can tell about a call it interrupted.
 *
 * <p>These pin existing behaviour rather than new behaviour, and they exist because something else
 * now depends on it. {@code one_d4}'s IndexWorker bounds a wedged indexing run by interrupting the
 * thread it is parked on, and it decides what happened by reading the <em>interrupt status</em> on
 * the way out rather than by matching an exception type — deliberately, since an interrupt arrives
 * in whatever shape the blocking call gives it. That makes the status a contract this library is
 * quietly party to, and nothing here was asserting it.
 *
 * <p>There is one blocking point now. The client waits for the complete response, so a caller is
 * only ever parked in {@code execute} — the body read that used to block separately is reading from
 * memory. That path throws {@link InterruptedException} with the status <em>cleared</em>, and
 * {@link Jdk11HttpClient} restoring it is the only reason the caller sees it at all.
 *
 * <p>So that one line is load-bearing rather than tidiness. Delete it and a wedged send unwinds
 * through the worker's ordinary failure path: an attempt spent, and a user told their range is
 * broken when the truth is that a worker was told to let go of it.
 *
 * <p>Real sockets throughout. A stub cannot block, and blocking is the entire subject.
 */
public class Jdk11HttpClientInterruptTest {

  /**
   * These tests set the interrupt status on threads, and JUnit hands the same thread to the next
   * case. Clearing it up front keeps each one standing on its own rather than on what its
   * predecessor left behind.
   */
  @BeforeEach
  public void clearAnyLeakedInterrupt() {
    Thread.interrupted();
  }

  /**
   * The load-bearing one: the JDK clears the status on this path, so the caller only sees it
   * because {@link Jdk11HttpClient} puts it back.
   */
  @Test
  @Timeout(60)
  public void anInterruptedSendComesBackWithTheInterruptStatusRestored() throws Exception {
    // Declared before the server so it is closed after it: shutting the peer's sockets first is
    // what guarantees this teardown cannot itself hang on the exchange the test just abandoned.
    try (HttpClient client = newClient();
        StalledServer server = new StalledServer(StalledServer.Behaviour.SILENT)) {
      InterruptProbe.Outcome outcome =
          interruptWhileBlocked(server.connected(), () -> client.execute(get(server)));

      assertThat(outcome.interruptStatusSet())
          .as(
              "a caller that keys on the interrupt status — as IndexWorker does — sees nothing"
                  + " unless this client restores what the JDK cleared on the way out")
          .isTrue();
      assertThat(outcome.thrown())
          .as("and the call has to come back at all; the peer was never going to answer")
          .isNotNull();
      assertThat(outcome.thrown())
          .as("the interrupt stays identifiable in the cause chain")
          .hasRootCauseInstanceOf(InterruptedException.class);
    }
  }

  /**
   * The negative that keeps the above honest. A body that stops short of its declared length is a
   * genuine transport failure, and on a thread nobody interrupted the status must stay clear —
   * otherwise "the status is set" would be true of every failure and would pin nothing.
   *
   * <p>Asserted on {@code execute} rather than on the body read: the response is complete before
   * execute returns, so a truncation surfaces there now.
   */
  @Test
  @Timeout(60)
  public void aTruncatedBodyOnAnUninterruptedThreadLeavesTheStatusClear() throws Exception {
    try (HttpClient client = newClient();
        StalledServer server = new StalledServer(StalledServer.Behaviour.HEAD_THEN_CLOSE)) {
      assertThatThrownBy(() -> client.execute(get(server)))
          .as("a body short of its Content-Length is a failure, not a cancellation")
          .isInstanceOf(RuntimeException.class);
      assertThat(Thread.currentThread().isInterrupted())
          .as("and nothing about it may look like an interrupt to a caller reading the status")
          .isFalse();
    }
  }

  private static HttpRequest get(StalledServer server) {
    return HttpRequest.newBuilder().setUrl(server.url()).build();
  }
}
