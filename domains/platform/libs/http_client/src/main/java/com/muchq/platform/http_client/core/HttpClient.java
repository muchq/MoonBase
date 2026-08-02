package com.muchq.platform.http_client.core;

import java.io.Closeable;

public interface HttpClient extends Closeable {
  /**
   * Sends a request and returns once the response head is available. The body is read separately,
   * through {@link HttpResponse}, and blocks on its own.
   *
   * <p>Interruptible, and implementations have to keep it that way. Nothing else bounds this call
   * or the body read that follows it — no timeout is applied by default, and a peer that accepts a
   * connection and then goes quiet produces no error to unblock on — so {@link Thread#interrupt()}
   * is the only way a caller can stop waiting. An implementation that swallows the interrupt, or
   * reports it as an ordinary I/O failure, takes that lever away: the caller's own cancellation
   * comes back indistinguishable from a broken transfer, so a thread that was told to stop either
   * keeps going or records a failure that never happened.
   *
   * @throws InterruptedRequestException if the calling thread is interrupted while waiting. The
   *     thread's interrupt status is set when it is thrown.
   */
  HttpResponse execute(HttpRequest request);

  HttpResponse executeAsync(HttpRequest request);
}
