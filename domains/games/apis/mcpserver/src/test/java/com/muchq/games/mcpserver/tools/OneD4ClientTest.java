package com.muchq.games.mcpserver.tools;

import static org.assertj.core.api.Assertions.assertThat;
import static org.assertj.core.api.Assertions.assertThatThrownBy;

import com.muchq.platform.http_client.jdk.Jdk11HttpClient;
import com.muchq.platform.json.JsonUtils;
import org.junit.jupiter.api.Test;

/**
 * What this client will accept as an upstream address.
 *
 * <p>The seam itself — paths, DTOs, status mapping — is covered against a real server in {@link
 * IndexerFacadeHttpTest}. This file is only about the base URL, because a URL this client cannot
 * use is a failure it cannot report usefully: {@code send} turns every {@link RuntimeException}
 * into "one_d4 is not reachable", which sends the reader to the network when the fault is in a
 * string.
 */
public class OneD4ClientTest {

  private static OneD4Client clientFor(String baseUrl) {
    return new OneD4Client(
        new Jdk11HttpClient(java.net.http.HttpClient.newHttpClient()), JsonUtils.mapper(), baseUrl);
  }

  /**
   * The bug this file was written for. {@code one_d4} is a legal Compose service name and Docker's
   * embedded DNS resolves it, so every non-Java client on that network reached it — but {@link
   * java.net.URI} follows RFC 3986's reg-name rule, gives an authority containing an underscore a
   * null host, and {@code java.net.http.HttpRequest} then rejects the URI with {@link
   * IllegalArgumentException} before opening a connection.
   *
   * <p>So it has to fail here, at construction, where the message can say which string is wrong.
   * Deferring it to the first call produced "one_d4 at http://one_d4:8080 is not reachable" from a
   * healthy one_d4, and the cause was discarded, so nothing in the logs contradicted it.
   */
  @Test
  public void refusesAnUpstreamHostJavaCannotParse() {
    assertThatThrownBy(() -> clientFor("http://one_d4:8080"))
        .isInstanceOf(IllegalArgumentException.class)
        .hasMessageContaining("http://one_d4:8080")
        .hasMessageContaining("host");
  }

  /** A URL with no host at all is the same class of fault and reads the same way. */
  @Test
  public void refusesAnUpstreamWithNoHost() {
    assertThatThrownBy(() -> clientFor("http:///v1/query"))
        .isInstanceOf(IllegalArgumentException.class)
        .hasMessageContaining("host");
  }

  /**
   * The guard rejects addresses Java cannot use, not addresses that merely look unusual. A check
   * strict enough to fail an ordinary Compose alias, a bare localhost or an IP literal would just
   * move the outage.
   */
  @Test
  public void acceptsTheAddressesADeploymentActuallyUses() {
    assertThat(clientFor("http://one-d4:8080").baseUrl()).isEqualTo("http://one-d4:8080");
    assertThat(clientFor("http://localhost:34567").baseUrl()).isEqualTo("http://localhost:34567");
    assertThat(clientFor("http://127.0.0.1:8080").baseUrl()).isEqualTo("http://127.0.0.1:8080");
    assertThat(clientFor("https://api.1d4.net").baseUrl()).isEqualTo("https://api.1d4.net");
  }

  /** Trailing-slash trimming still happens, and happens before validation rather than instead. */
  @Test
  public void stillTrimsATrailingSlash() {
    assertThat(clientFor("http://one-d4:8080/").baseUrl()).isEqualTo("http://one-d4:8080");
  }

  /**
   * "Not reachable" says the address was fine and the peer did not answer. That is a claim about
   * something, and when it is wrong the reader has nowhere to go: the cause was on the exception
   * but nothing rendered it, so the only text anyone saw named a service that was healthy.
   *
   * <p>Naming the underlying failure in the message costs one line and is the difference between
   * "check the network" and "read the exception".
   */
  @Test
  public void saysWhyItCouldNotReachOneD4() throws Exception {
    int deadPort;
    try (java.net.ServerSocket socket = new java.net.ServerSocket(0)) {
      deadPort = socket.getLocalPort();
    }
    OneD4Client client = clientFor("http://localhost:" + deadPort);

    assertThatThrownBy(() -> client.status(java.util.UUID.randomUUID()))
        .isInstanceOf(OneD4Client.UpstreamException.class)
        .hasMessageContaining("is not reachable")
        .hasMessageContaining("ConnectException");
  }
}
