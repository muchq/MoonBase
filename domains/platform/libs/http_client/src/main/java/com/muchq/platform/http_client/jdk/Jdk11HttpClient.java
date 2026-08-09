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
   * Sends and waits for the complete response.
   *
   * <p>Asynchronous underneath, with {@code ofByteArray}, so one deadline covers the whole exchange
   * — {@code orTimeout} fires whether the peer stalls before the head or halfway through the body.
   * The blocking {@code send} could not do that: its request timeout ends when the head arrives and
   * the body streams afterwards, unbounded (#1336).
   *
   * <p>The cost is that the body is buffered rather than streamed. Accepted deliberately: callers
   * here parse into object trees that are larger than the bytes anyway, so the saving was smaller
   * than it looked, and the alternative was a reader pool plus a close-to-unblock trick to make a
   * blocked socket read answer to a deadline at all.
   */
  @Override
  public HttpResponse execute(HttpRequest request) {
    java.net.http.HttpRequest httpRequest = toJdk11HttpRequest(request);

    java.util.concurrent.CompletableFuture<java.net.http.HttpResponse<byte[]>> pending =
        delegate.sendAsync(httpRequest, java.net.http.HttpResponse.BodyHandlers.ofByteArray());
    java.util.Optional<java.time.Duration> deadline = request.getTimeout();
    if (deadline.isPresent()) {
      pending =
          pending.orTimeout(deadline.get().toNanos(), java.util.concurrent.TimeUnit.NANOSECONDS);
    }

    try {
      return new Jdk11HttpResponse(request, pending.get());
    } catch (InterruptedException e) {
      // Restored deliberately, and load-bearing: get() clears the status, and one_d4's IndexWorker
      // decides a wedged run was cancelled by reading that status rather than by exception type.
      // Without this line a cancelled run unwinds as an ordinary failure and burns an attempt.
      Thread.currentThread().interrupt();
      pending.cancel(true);
      throw new RuntimeException(e);
    } catch (java.util.concurrent.ExecutionException e) {
      Throwable cause = e.getCause();
      throw unwrap(cause == null ? e : cause);
    }
  }

  /**
   * {@code orTimeout} fails the future with a bare {@link java.util.concurrent.TimeoutException};
   * translated so a deadline looks the same to callers however it was reached, and so a timeout
   * stays distinguishable from a truncated body.
   */
  private static RuntimeException unwrap(Throwable cause) {
    if (cause instanceof java.util.concurrent.TimeoutException) {
      return new UncheckedIOException(
          new java.net.http.HttpTimeoutException("request did not complete within the deadline"));
    }
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

    // Also set on the request: it fails faster and more precisely than the outer deadline when the
    // peer never answers at all, and costs nothing when the outer one is what fires.
    request.getTimeout().ifPresent(builder::timeout);

    return builder.build();
  }
}
