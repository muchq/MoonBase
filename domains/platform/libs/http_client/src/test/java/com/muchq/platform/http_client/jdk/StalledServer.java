package com.muchq.platform.http_client.jdk;

import static java.nio.charset.StandardCharsets.UTF_8;

import java.io.Closeable;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.net.InetAddress;
import java.net.ServerSocket;
import java.net.Socket;
import java.util.List;
import java.util.concurrent.CopyOnWriteArrayList;
import java.util.concurrent.CountDownLatch;

/**
 * A peer that stops talking, which is the only shape of failure the interrupt contract is for.
 *
 * <p>Every other way an HTTP call can go wrong ends by itself: a refused connection, a reset, a
 * truncated body all produce an exception and unblock the caller. A peer that completes the
 * handshake and then goes quiet produces nothing at all — no bytes, no error, no EOF — and with no
 * timeout configured the call waits forever. That is the state the tests need to reproduce, and it
 * cannot be reproduced with a stub: the thread has to be genuinely parked on a socket.
 *
 * <p>Three behaviours, covering the two places a caller can be parked and the negative case that
 * keeps the tests honest.
 */
final class StalledServer implements Closeable {

  /** Where the caller is left waiting. */
  enum Behaviour {
    /** Accepts the connection and never answers: the caller is parked in {@code execute}. */
    SILENT,
    /**
     * Answers with a complete head promising a body, sends a few bytes of it, then goes quiet: the
     * caller is parked in the body read, having already seen a successful response.
     */
    HEAD_THEN_STALL,
    /**
     * The same, but hangs up instead of stalling. A real failure — the body is short of its
     * declared length — and the one that must <em>not</em> be reported as an interrupt.
     */
    HEAD_THEN_CLOSE
  }

  /** Declared in the response head, and deliberately never delivered in full. */
  private static final int DECLARED_BODY_LENGTH = 4096;

  private static final byte[] PARTIAL_BODY = "{\"games\":[".getBytes(UTF_8);

  private final ServerSocket serverSocket;
  private final CountDownLatch connected = new CountDownLatch(1);
  private final CountDownLatch bodyStarted = new CountDownLatch(1);
  private final CountDownLatch clientHungUp = new CountDownLatch(1);

  /** Held open on purpose: closing them would unblock the caller and defeat the whole fixture. */
  private final List<Socket> open = new CopyOnWriteArrayList<>();

  StalledServer(Behaviour behaviour) throws IOException {
    this.serverSocket = new ServerSocket(0, 8, InetAddress.getLoopbackAddress());
    Thread acceptor = new Thread(() -> acceptLoop(behaviour), "stalled-server");
    acceptor.setDaemon(true);
    acceptor.start();
  }

  String url() {
    return "http://127.0.0.1:" + serverSocket.getLocalPort() + "/archive";
  }

  /** Trips once a client has connected — the point from which it is definitely waiting on us. */
  CountDownLatch connected() {
    return connected;
  }

  /** Trips once the response head and the first few body bytes have gone out. */
  CountDownLatch bodyStarted() {
    return bodyStarted;
  }

  /**
   * Trips when the client's end of a stalled exchange goes away — EOF or a reset on the socket this
   * server is still holding open.
   *
   * <p>The one observation that distinguishes an abandoned exchange from a cancelled one. A client
   * whose deadline expires without cancelling leaves this latch untripped: its caller has been sent
   * on its way, but the socket is still there, still subscribed, still buffering whatever the peer
   * chooses to send. Only a real cancellation reaches the far end.
   */
  CountDownLatch clientHungUp() {
    return clientHungUp;
  }

  private void acceptLoop(Behaviour behaviour) {
    while (!serverSocket.isClosed()) {
      try {
        Socket socket = serverSocket.accept();
        open.add(socket);
        connected.countDown();
        if (behaviour != Behaviour.SILENT) {
          respondPartially(socket, behaviour);
        }
        if (behaviour != Behaviour.HEAD_THEN_CLOSE) {
          watchForHangup(socket);
        }
      } catch (IOException e) {
        return; // The socket was closed: the test is over.
      }
    }
  }

  private void respondPartially(Socket socket, Behaviour behaviour) throws IOException {
    consumeRequestHead(socket.getInputStream());
    OutputStream out = socket.getOutputStream();
    out.write(
        ("HTTP/1.1 200 OK\r\n"
                + "Content-Type: application/json\r\n"
                + "Content-Length: "
                + DECLARED_BODY_LENGTH
                + "\r\n\r\n")
            .getBytes(UTF_8));
    out.write(PARTIAL_BODY);
    out.flush();
    if (behaviour == Behaviour.HEAD_THEN_CLOSE) {
      socket.close();
      open.remove(socket);
    }
    bodyStarted.countDown();
  }

  /**
   * Drains whatever the client sends until the connection ends, on its own thread so the accept
   * loop stays free. The bytes are not the point — reaching the end of them is.
   */
  private void watchForHangup(Socket socket) {
    Thread watcher =
        new Thread(
            () -> {
              try {
                InputStream in = socket.getInputStream();
                while (in.read() != -1) {
                  // The request head, and then nothing: a stalled client sends no more.
                }
              } catch (IOException reset) {
                // A reset is a hangup too, and reads as one.
              } finally {
                clientHungUp.countDown();
              }
            },
            "stalled-server-watch");
    watcher.setDaemon(true);
    watcher.start();
  }

  /** Reads up to the blank line, so the client's write completes and it settles into the wait. */
  private static void consumeRequestHead(InputStream in) throws IOException {
    int consecutiveNewlines = 0;
    int b;
    while (consecutiveNewlines < 2 && (b = in.read()) != -1) {
      if (b == '\n') {
        consecutiveNewlines++;
      } else if (b != '\r') {
        consecutiveNewlines = 0;
      }
    }
  }

  @Override
  public void close() throws IOException {
    for (Socket socket : open) {
      try {
        socket.close();
      } catch (IOException ignored) {
        // Best effort: the test is finished with it either way.
      }
    }
    open.clear();
    serverSocket.close();
  }

  /** A free port with nothing behind it, for the connection-refused case. */
  static String unusedPortUrl() throws IOException {
    int port;
    try (ServerSocket probe = new ServerSocket(0, 1, InetAddress.getLoopbackAddress())) {
      port = probe.getLocalPort();
    }
    return "http://127.0.0.1:" + port + "/archive";
  }
}
