package com.muchq.platform.http_client.core;

/**
 * Thrown when a blocking call was abandoned because its thread was interrupted, rather than because
 * anything went wrong with the request.
 *
 * <p>An HTTP call is where a thread waits, and waiting is the one thing a caller sometimes needs to
 * stop doing. Nothing bounds a peer that has accepted a connection and gone quiet: no timeout is
 * set by default, and a half-open socket produces no error to unblock on. The only lever left is
 * {@link Thread#interrupt()}, and it is worth nothing unless the interrupt is distinguishable when
 * it arrives — a caller that cannot tell "I asked to stop" from "the transfer broke" has to treat
 * its own cancellation as a failure, which is how a deliberate stop turns into a retry, an error
 * report, and a spent attempt against whatever was being fetched.
 *
 * <p>So this type exists to say which one happened. It is unchecked, because it can come out of any
 * blocking point in a call — the send, or any read of the response body — and forcing every caller
 * to declare it would say nothing they can act on. Callers that genuinely have to distinguish catch
 * it; the rest let it unwind, which is the right default for a thread that has been told to stop.
 *
 * <p>The thread's interrupt status is always set when this is thrown, so a caller that catches it
 * and keeps going still sees the interrupt at its next blocking point. That is the invariant {@link
 * #restoringInterruptStatus} exists to make unforgettable: the flag is restored by the same
 * expression that builds the exception, so no throw site can get one without the other.
 */
public class InterruptedRequestException extends RuntimeException {

  private InterruptedRequestException(String message, Throwable cause) {
    super(message, cause);
  }

  /**
   * Restores this thread's interrupt status and returns the exception to throw.
   *
   * <p>Written to be used as {@code throw InterruptedRequestException.restoringInterruptStatus(…)}.
   * Both halves have to happen and the order does not matter, which is exactly the shape that gets
   * half-done when it is two statements: catching {@link InterruptedException} clears the flag, so
   * a throw site that forgets to put it back leaves the thread looking uninterrupted to everything
   * upstream — and the next blocking call it makes will wait out its full timeout.
   */
  public static InterruptedRequestException restoringInterruptStatus(
      String message, Throwable cause) {
    Thread.currentThread().interrupt();
    return new InterruptedRequestException(message, cause);
  }
}
