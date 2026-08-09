package com.muchq.platform.http_client.jdk;

import java.io.IOException;
import java.io.InputStream;
import java.io.InterruptedIOException;
import java.net.http.HttpTimeoutException;
import java.time.Duration;
import java.util.concurrent.ExecutionException;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;
import java.util.concurrent.Future;
import java.util.concurrent.ThreadFactory;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.TimeoutException;
import java.util.concurrent.atomic.AtomicInteger;

/**
 * A response body that cannot be read forever.
 *
 * <p>Wraps the stream the JDK client hands back and puts a wall-clock ceiling on the whole body,
 * not on each read: a peer that dribbles one byte per second would otherwise satisfy any per-read
 * deadline indefinitely.
 *
 * <p>The mechanism is the interesting part. A blocked {@code read()} on a socket stream honours no
 * timeout and does not answer to interruption, so the read happens on a pool thread and the caller
 * waits on it with a bound. When that bound expires the delegate is <em>closed</em> — that is what
 * unblocks the parked reader, since a socket read fails once its stream is closed. Cancelling the
 * future alone would leave the thread stuck and leak one per stalled response, which is the bug
 * this exists to prevent rather than relocate.
 */
final class DeadlineInputStream extends InputStream {

  private static final AtomicInteger THREAD_COUNTER = new AtomicInteger();

  /**
   * Shared and daemon: reads are short and the pool exists only so a blocked one is not the
   * caller's thread. Cached rather than fixed because a bounded pool would make one stalled
   * response able to delay an unrelated healthy one.
   */
  private static final ExecutorService READERS =
      Executors.newCachedThreadPool(
          new ThreadFactory() {
            @Override
            public Thread newThread(Runnable r) {
              Thread t = new Thread(r, "http-body-read-" + THREAD_COUNTER.incrementAndGet());
              t.setDaemon(true);
              return t;
            }
          });

  private final InputStream delegate;
  private final long deadlineNanos;
  private final Duration timeout;

  DeadlineInputStream(InputStream delegate, Duration timeout) {
    this.delegate = delegate;
    this.timeout = timeout;
    this.deadlineNanos = System.nanoTime() + timeout.toNanos();
  }

  @Override
  public int read() throws IOException {
    byte[] one = new byte[1];
    int read = read(one, 0, 1);
    return read == -1 ? -1 : one[0] & 0xff;
  }

  @Override
  public int read(byte[] buffer, int offset, int length) throws IOException {
    long remaining = deadlineNanos - System.nanoTime();
    if (remaining <= 0) {
      abandon();
      throw timedOut();
    }

    Future<Integer> pending = READERS.submit(() -> delegate.read(buffer, offset, length));
    try {
      return pending.get(remaining, TimeUnit.NANOSECONDS);
    } catch (TimeoutException e) {
      // Close first, then cancel: closing is what ends the blocked read, and the pool thread
      // finishes on its own once it does.
      abandon();
      pending.cancel(true);
      throw timedOut();
    } catch (ExecutionException e) {
      Throwable cause = e.getCause();
      if (cause instanceof IOException io) {
        throw io;
      }
      if (cause instanceof RuntimeException runtime) {
        throw runtime;
      }
      throw new IOException("body read failed", cause);
    } catch (InterruptedException e) {
      Thread.currentThread().interrupt();
      abandon();
      throw new InterruptedIOException("interrupted while reading the response body");
    }
  }

  @Override
  public void close() throws IOException {
    delegate.close();
  }

  private void abandon() {
    try {
      delegate.close();
    } catch (IOException ignored) {
      // Already broken; the caller is getting a timeout either way.
    }
  }

  private HttpTimeoutException timedOut() {
    return new HttpTimeoutException("response body not fully read within " + timeout);
  }
}
