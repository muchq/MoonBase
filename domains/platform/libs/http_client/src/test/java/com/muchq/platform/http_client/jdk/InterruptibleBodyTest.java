package com.muchq.platform.http_client.jdk;

import static com.muchq.platform.http_client.jdk.InterruptProbe.interruptWhileBlocked;
import static com.muchq.platform.http_client.jdk.InterruptProbe.newClient;
import static org.assertj.core.api.Assertions.assertThat;
import static org.assertj.core.api.Assertions.assertThatThrownBy;

import com.muchq.platform.http_client.core.HttpClient;
import com.muchq.platform.http_client.core.HttpRequest;
import com.muchq.platform.http_client.core.HttpResponse;
import com.muchq.platform.http_client.core.InterruptedRequestException;
import java.io.IOException;
import java.io.InputStream;
import org.junit.jupiter.api.BeforeEach;
import org.junit.jupiter.api.Test;
import org.junit.jupiter.api.Timeout;

/**
 * The other half of the wait, and the half that is easier to overlook.
 *
 * <p>{@code execute} returning does not mean the call is over: with a streaming body handler it
 * returns as soon as the response head arrives, and everything after that — every byte of the body
 * — is read by the caller, blocking, with the same absence of any bound. A peer that sends a
 * plausible {@code 200 OK} and then stops writing parks the caller just as thoroughly as one that
 * never answered, and from further inside, because by then the caller has a successful response in
 * hand and has handed the stream to a parser.
 *
 * <p>That last part is why the interrupt is reported from the stream rather than from a convenience
 * method. The callers that matter never touch {@code getAsBytes}; they pass the stream to Jackson
 * and read the object back, so an interrupt that only {@code getAsBytes} translated would never
 * reach them.
 *
 * <p>Not because the fact would otherwise be unrecoverable — the JDK leaves {@code
 * IOException(InterruptedException)} behind and a caller could walk it. Because doing so couples
 * every caller to a wrapping convention {@code BodyHandlers} does not document, and because the
 * cheaper caller-side check — the thread's interrupt status — answers a question about the thread
 * when the question is about the read. The last two tests here are that distinction.
 */
public class InterruptibleBodyTest {

  /**
   * Several tests here set the interrupt status on the thread running them, and JUnit hands that
   * same thread to the next one. Clearing it up front keeps each case standing on its own, rather
   * than passing or failing on what its predecessor left behind.
   */
  @BeforeEach
  public void clearAnyLeakedInterrupt() {
    Thread.interrupted();
  }

  /** Parked in the body read, told to stop, and it stops. */
  @Test
  @Timeout(60)
  public void interruptingABodyReadAbortsIt() throws Exception {
    try (HttpClient client = newClient();
        StalledServer server = new StalledServer(StalledServer.Behaviour.HEAD_THEN_STALL)) {
      HttpResponse response = client.execute(get(server));
      assertThat(response.getStatusCode())
          .as("the head arrived: what is under test is the wait that comes after it")
          .isEqualTo(200);

      InterruptProbe.Outcome outcome =
          interruptWhileBlocked(server.bodyStarted(), response::getAsBytes);

      assertThat(outcome.thrown()).isInstanceOf(InterruptedRequestException.class);
      assertThat(outcome.interruptStatusSet()).isTrue();
    }
  }

  /**
   * The same interrupt, reaching a caller that never uses this library's response API — which is
   * every caller that hands the body to a parser. Reading the raw stream is exactly what Jackson
   * does, so if the interrupt does not come out of {@code read} it does not come out at all.
   */
  @Test
  @Timeout(60)
  public void theInterruptSurfacesThroughTheRawBodyStream() throws Exception {
    try (HttpClient client = newClient();
        StalledServer server = new StalledServer(StalledServer.Behaviour.HEAD_THEN_STALL)) {
      HttpResponse response = client.execute(get(server));
      InputStream body = response.getAsInputStream();

      InterruptProbe.Outcome outcome =
          interruptWhileBlocked(server.bodyStarted(), () -> drain(body));

      assertThat(outcome.thrown()).isInstanceOf(InterruptedRequestException.class);
      assertThat(outcome.interruptStatusSet()).isTrue();
    }
  }

  /**
   * The negative, and the one that keeps the wrapper honest. A body that stops short of its
   * declared length is a broken transfer — the caller wanted those bytes and did not get them — and
   * it must not be laundered into "you asked to stop". A wrapper that reported every read failure
   * as an interrupt would satisfy both tests above.
   */
  @Test
  @Timeout(60)
  public void aTruncatedBodyIsStillReportedAsAFailure() throws Exception {
    try (HttpClient client = newClient();
        StalledServer server = new StalledServer(StalledServer.Behaviour.HEAD_THEN_CLOSE)) {
      HttpResponse response = client.execute(get(server));

      assertThatThrownBy(response::getAsBytes).isNotInstanceOf(InterruptedRequestException.class);
      assertThat(Thread.currentThread().isInterrupted())
          .as("a transport failure must not leave the caller's thread looking interrupted")
          .isFalse();
    }
  }

  /**
   * The signal, pinned on its own, on a thread whose status has already been consumed.
   *
   * <p>The cause is a fact about the read; the interrupt status is a fact about the thread, and the
   * two come apart in both directions. {@link Thread#interrupted()} clears the flag and plenty of
   * code calls it — including a caller that has already caught one of these and is unwinding
   * through a second read — so the chain has to be enough by itself. (The other direction, a
   * genuine failure on a thread interrupted for unrelated reasons, is why the status is not
   * consulted at all: see the test below.)
   */
  @Test
  @Timeout(30)
  public void aReadCarryingAnInterruptedCauseIsHonouredEvenWithTheStatusAlreadyCleared() {
    InputStream body = new InterruptibleBody(new BrokenStream(new InterruptedException("gone")));
    assertThat(Thread.currentThread().isInterrupted())
        .as("precondition: nothing but the cause chain is left to go on")
        .isFalse();

    assertThatThrownBy(body::read).isInstanceOf(InterruptedRequestException.class);
    assertThat(Thread.interrupted()).as("and the status is put back").isTrue();
  }

  /** And the converse, so the tests above are not passing for every failure alike. */
  @Test
  @Timeout(30)
  public void aReadThatFailsOnAnUninterruptedThreadIsPassedThroughUnchanged() {
    InputStream body = new InterruptibleBody(new BrokenStream());

    assertThatThrownBy(body::read)
        .isInstanceOf(IOException.class)
        .isNotInstanceOf(InterruptedRequestException.class)
        .hasMessage("socket closed");
  }

  /**
   * The sharper converse: a genuine failure on a thread that happens to be interrupted is still a
   * genuine failure.
   *
   * <p>Consulting the interrupt status here is the tempting shortcut, and it is wrong for a reason
   * that only shows up in this shape. The status says something about the <em>thread</em> — it can
   * be set by a shutdown, by an unrelated cancellation, by whatever else that thread is enrolled in
   * — while the question being asked is about this <em>read</em>. Answer it from the status and a
   * truncated body becomes "you asked to stop", so the caller unwinds quietly instead of retrying
   * or reporting a transfer that really did break.
   */
  @Test
  @Timeout(30)
  public void aGenuineFailureOnAnInterruptedThreadIsNotRelabelledACancellation() {
    InputStream body = new InterruptibleBody(new BrokenStream());
    Thread.currentThread().interrupt();

    assertThatThrownBy(body::read)
        .isInstanceOf(IOException.class)
        .isNotInstanceOf(InterruptedRequestException.class)
        .hasMessage("socket closed");
    assertThat(Thread.interrupted())
        .as("and the unrelated interrupt is left exactly as it was found")
        .isTrue();
  }

  // --- wiring ---------------------------------------------------------------------------------

  private static HttpRequest get(StalledServer server) {
    return HttpRequest.newBuilder().setUrl(server.url()).build();
  }

  /** Reads to the end, the way a parser would, so the interrupt has to come out of {@code read}. */
  private static void drain(InputStream body) {
    try {
      while (body.read() != -1) {
        // Discarding: the bytes are not the point, the blocking is.
      }
    } catch (IOException e) {
      throw new IllegalStateException(e);
    }
  }

  /** A body that fails the way a socket closed underneath a read does. */
  private static final class BrokenStream extends InputStream {
    private final Throwable cause;

    /** No cause and no explanation, which is the case the interrupt status has to carry alone. */
    BrokenStream() {
      this(null);
    }

    BrokenStream(Throwable cause) {
      this.cause = cause;
    }

    @Override
    public int read() throws IOException {
      throw cause == null ? new IOException("socket closed") : new IOException(cause);
    }
  }
}
