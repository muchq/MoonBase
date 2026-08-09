package com.muchq.platform.http_client.jdk;

import com.muchq.platform.http_client.core.HttpClient;
import com.muchq.platform.http_client.core.HttpRequest;
import com.muchq.platform.http_client.core.HttpResponse;
import java.io.IOException;
import java.io.UncheckedIOException;
import java.util.Objects;

public class Jdk11HttpClient implements HttpClient {

  private final java.net.http.HttpClient delegate;

  public Jdk11HttpClient(java.net.http.HttpClient delegate) {
    this.delegate = Objects.requireNonNull(delegate);
  }

  /**
   * Sends and waits for the complete response, bounded by {@link HttpRequest#getTimeout()}.
   *
   * <p>This is the only deadline in the stack, on purpose. Asynchronous underneath, with {@code
   * ofByteArray}, so the wait covers the whole exchange — it expires whether the peer stalls before
   * the head or halfway through the body. The blocking {@code send} could not do that: its request
   * timeout ends when the head arrives and the body streams afterwards, unbounded (#1336).
   * Consumers used to wrap their own timeout around this call to cover the gap; they no longer
   * should, because two deadlines over one call race and hand the caller whichever exception won.
   *
   * <p>The cost is that the body is buffered rather than streamed. Accepted deliberately: callers
   * here parse into object trees that are larger than the bytes anyway, so the saving was smaller
   * than it looked, and the alternative was a reader pool plus a close-to-unblock trick to make a
   * blocked socket read answer to a deadline at all.
   */
  @Override
  public HttpResponse execute(HttpRequest request) {
    java.net.http.HttpRequest httpRequest = toJdk11HttpRequest(request);
    java.util.Optional<java.time.Duration> deadline = request.getTimeout();

    java.util.concurrent.CompletableFuture<java.net.http.HttpResponse<byte[]>> pending =
        delegate.sendAsync(httpRequest, java.net.http.HttpResponse.BodyHandlers.ofByteArray());

    try {
      java.net.http.HttpResponse<byte[]> response =
          deadline.isPresent()
              ? pending.get(deadline.get().toNanos(), java.util.concurrent.TimeUnit.NANOSECONDS)
              : pending.get();
      return new Jdk11HttpResponse(request, response);
    } catch (java.util.concurrent.TimeoutException e) {
      // A timed get, rather than orTimeout, so this cancel lands on a future that is still
      // running. orTimeout completes the future itself, and cancelling an already-completed
      // future is a no-op that returns false — the caller would come back while the exchange
      // stayed alive underneath, still buffering, and close() would then wait on work the caller
      // believes it abandoned.
      pending.cancel(true);
      throw new UncheckedIOException(
          new java.net.http.HttpTimeoutException("request did not complete within the deadline"));
    } catch (InterruptedException e) {
      // Restored deliberately, and load-bearing: get() clears the status, and one_d4's IndexWorker
      // decides a wedged run was cancelled by reading that status rather than by exception type.
      // Without this line a cancelled run unwinds as an ordinary failure and burns an attempt.
      //
      // Only a caller's own interrupt reaches here. Nothing inside this client interrupts the
      // calling thread, so the status a caller reads afterwards means what IndexWorker takes it to
      // mean — an expired deadline is a TimeoutException above, on a thread left uninterrupted.
      Thread.currentThread().interrupt();
      pending.cancel(true);
      throw new RuntimeException(e);
    } catch (java.util.concurrent.ExecutionException e) {
      Throwable cause = e.getCause();
      throw unwrap(cause == null ? e : cause);
    }
  }

  /**
   * The request-level timeout below fails the future rather than the get, so a deadline reached
   * that way arrives here as {@link java.net.http.HttpTimeoutException} — already an IOException,
   * and wrapped into the same {@link UncheckedIOException} the timed get throws. Both routes to
   * "too slow" look identical to a caller, which is what lets the two coexist without the caller
   * having to know which fired.
   */
  private static RuntimeException unwrap(Throwable cause) {
    if (cause instanceof IOException io) {
      return new UncheckedIOException(io);
    }
    if (cause instanceof RuntimeException runtime) {
      return runtime;
    }
    return new RuntimeException(cause);
  }

  @Override
  public HttpResponse executeAsync(HttpRequest request) {
    throw new UnsupportedOperationException();
  }

  @Override
  public void close() {
    delegate.close();
  }

  private java.net.http.HttpRequest toJdk11HttpRequest(HttpRequest request) {
    var builder = java.net.http.HttpRequest.newBuilder().uri(request.getUrl());
    request.getHeaders().forEach(h -> builder.setHeader(h.getName(), h.getValue()));

    if (!request.getMethod().allowsBody() || request.getBody() == null) {
      builder.method(request.getMethod().name(), java.net.http.HttpRequest.BodyPublishers.noBody());
    } else {
      builder.method(
          request.getMethod().name(),
          java.net.http.HttpRequest.BodyPublishers.ofByteArray(request.getBody()));
    }

    // Also set on the request: it fails faster and more precisely than the timed get when the peer
    // never answers at all — a refused or unanswered connect is reported as such — and costs
    // nothing when the get is what fires. Safe to have both only because they agree on the
    // exception a caller sees; see unwrap.
    request.getTimeout().ifPresent(builder::timeout);

    return builder.build();
  }
}
