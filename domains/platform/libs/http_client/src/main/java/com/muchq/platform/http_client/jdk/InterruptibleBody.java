package com.muchq.platform.http_client.jdk;

import com.muchq.platform.http_client.core.InterruptedRequestException;
import java.io.FilterInputStream;
import java.io.IOException;
import java.io.InputStream;
import java.nio.channels.ClosedByInterruptException;

/**
 * A response body that reports an interrupt as an interrupt.
 *
 * <p>The body read is the half of an HTTP call a stuck caller is most likely to be parked in. The
 * request went out long ago, the status line and headers arrived, and then the peer stopped writing
 * — no error, no timeout, nothing to unblock on but {@link Thread#interrupt()}.
 *
 * <p>The JDK's body stream does respond to that interrupt, and it does leave enough behind to
 * identify: {@code IOException(InterruptedException)}, with the thread's interrupt status still
 * set. So this is not about recovering lost information — a caller could walk that cause chain
 * itself. It is about not making every caller do it, against a convention nothing promises. {@code
 * BodyHandlers.ofInputStream()} does not document the wrapping; it is a detail of {@code
 * HttpResponseInputStream}, and a caller coupled to it breaks silently if the JDK ever reports the
 * same event as, say, a {@link ClosedByInterruptException}.
 *
 * <p>What the type buys, beyond convenience, is precision. The cheap caller-side check is the
 * thread's interrupt status, and that decides an <em>event</em> from a property of the
 * <em>thread</em> — a real truncated-body failure, arriving on a thread interrupted for an
 * unrelated reason, reads as a cancellation that never happened.
 *
 * <p>Sitting on the stream rather than on {@code getAsBytes} is deliberate: the callers that matter
 * hand the stream to a parser and never touch the byte array, and an interrupt has to reach them
 * too.
 */
final class InterruptibleBody extends FilterInputStream {

  InterruptibleBody(InputStream body) {
    super(body);
  }

  @Override
  public int read() throws IOException {
    try {
      return in.read();
    } catch (IOException e) {
      throw translate(e);
    }
  }

  @Override
  public int read(byte[] b, int off, int len) throws IOException {
    try {
      return in.read(b, off, len);
    } catch (IOException e) {
      throw translate(e);
    }
  }

  @Override
  public long skip(long n) throws IOException {
    try {
      return in.skip(n);
    } catch (IOException e) {
      throw translate(e);
    }
  }

  /**
   * Returns the exception to rethrow, or throws {@link InterruptedRequestException} if the read
   * stopped because this thread was interrupted.
   *
   * <p>Written as {@code throw translate(e)} so the compiler can see that the read never falls
   * through, whichever branch is taken.
   *
   * <p>The cause chain, and only the cause chain. The obvious second signal is the thread's
   * interrupt status, which the JDK does set here — but reading it would decide an <em>event</em>
   * from a property of the <em>thread</em>, and those come apart: a genuine truncated-body failure
   * arriving on a thread interrupted for some unrelated reason would be reported as a cancellation
   * that never happened. The cause is a fact about this read.
   *
   * <p>{@link ClosedByInterruptException} is included because a stream backed by an interruptible
   * channel reports the same event that way, and it is equally a fact about the read rather than
   * about the thread.
   */
  private static IOException translate(IOException e) {
    if (causedByInterrupt(e)) {
      throw InterruptedRequestException.restoringInterruptStatus(
          "Interrupted while reading the response body", e);
    }
    return e;
  }

  private static boolean causedByInterrupt(Throwable e) {
    Throwable cause = e;
    while (cause != null) {
      if (cause instanceof InterruptedException || cause instanceof ClosedByInterruptException) {
        return true;
      }
      cause = cause.getCause();
    }
    return false;
  }
}
