package com.muchq.platform.http_client.jdk;

import static org.assertj.core.api.Assertions.assertThat;

import com.muchq.platform.http_client.core.HttpClient;
import java.util.concurrent.CountDownLatch;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.atomic.AtomicBoolean;
import java.util.concurrent.atomic.AtomicReference;
import org.jspecify.annotations.Nullable;

/**
 * Runs a blocking call on its own thread, interrupts it, and reports what came out.
 *
 * <p>Shared by both interrupt suites, and separate from either because {@code java_test_suite}
 * compiles every {@code *Test} class into its own target — a helper living in one of them is
 * invisible to the other.
 */
final class InterruptProbe {

  private InterruptProbe() {}

  /**
   * @param thrown null when the call returned normally — which is itself a result worth reporting,
   *     since a client that swallows the interrupt and returns is the failure these suites hunt.
   */
  record Outcome(@Nullable Throwable thrown, boolean interruptStatusSet) {}

  /**
   * Runs {@code blockingCall} on its own thread, interrupts it once {@code parked} says it has
   * reached the server, and reports what came out.
   *
   * <p>The wait for the call to return is the sharpest assertion in any of these tests: the peer
   * never answers, so a call that ignored the interrupt would simply never come back.
   */
  static Outcome interruptWhileBlocked(CountDownLatch parked, Runnable blockingCall)
      throws InterruptedException {
    AtomicReference<Throwable> thrown = new AtomicReference<>();
    AtomicBoolean interruptStatus = new AtomicBoolean();
    CountDownLatch returned = new CountDownLatch(1);

    Thread caller =
        new Thread(
            () -> {
              try {
                blockingCall.run();
              } catch (Throwable t) {
                thrown.set(t);
              } finally {
                interruptStatus.set(Thread.currentThread().isInterrupted());
                returned.countDown();
              }
            },
            "interrupt-probe");
    caller.setDaemon(true);
    caller.start();

    assertThat(parked.await(30, TimeUnit.SECONDS))
        .as("the call should have reached the server before it was interrupted")
        .isTrue();
    caller.interrupt();

    assertThat(returned.await(30, TimeUnit.SECONDS))
        .as("an interrupted call has to return; this one was still waiting on a silent peer")
        .isTrue();
    return new Outcome(thrown.get(), interruptStatus.get());
  }

  /**
   * No proxy, deliberately. A build environment that exports proxy settings would otherwise put a
   * third party between this client and its own loopback socket, and every fixture here depends on
   * the peer's silence being the only thing the caller is waiting for.
   */
  static HttpClient newClient() {
    return new Jdk11HttpClient(
        java.net.http.HttpClient.newBuilder()
            .proxy(java.net.http.HttpClient.Builder.NO_PROXY)
            .build());
  }
}
