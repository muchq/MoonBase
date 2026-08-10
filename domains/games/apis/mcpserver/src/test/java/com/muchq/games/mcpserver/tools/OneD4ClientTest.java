package com.muchq.games.mcpserver.tools;

import static org.assertj.core.api.Assertions.assertThat;
import static org.assertj.core.api.Assertions.assertThatThrownBy;

import com.muchq.games.one_d4.api.dto.QueryRequest;
import com.muchq.platform.http_client.jdk.Jdk11HttpClient;
import com.muchq.platform.json.JsonUtils;
import org.junit.jupiter.api.Test;

/**
 * What this client will accept as an upstream address.
 *
 * <p>The seam itself — paths, DTOs, status mapping — is covered against a real server in {@link
 * IndexerFacadeHttpTest}. This file is only about the base URL and how a bad one is reported.
 */
public class OneD4ClientTest {

  private static OneD4Client clientFor(String baseUrl) {
    return new OneD4Client(
        new Jdk11HttpClient(java.net.http.HttpClient.newHttpClient()), JsonUtils.mapper(), baseUrl);
  }

  /**
   * {@code one_d4} is a legal Compose service name that Docker resolves, but {@link java.net.URI}
   * gives an authority containing an underscore a null host, so no request built from it can be
   * sent.
   *
   * <p>What the caller is told is the point. Not "not reachable", which is a claim about the peer,
   * and not a bare {@link IllegalArgumentException}, which this class uses for "fix your arguments"
   * and would blame an MCP client's query for the deployment's URL.
   */
  @Test
  public void reportsAnUnusableBaseUrlAsConfigurationRatherThanAsADeadPeer() {
    OneD4Client client = clientFor("http://one_d4:8080");

    assertThatThrownBy(() -> client.query(new QueryRequest("eco = \"B90\"", 10, 0)))
        .isInstanceOf(OneD4Client.UpstreamException.class)
        .hasMessageContaining("http://one_d4:8080")
        .hasMessageContaining("not usable")
        .hasMessageNotContaining("not reachable")
        .hasMessageNotContaining("serialize");
  }

  /** The GET path builds its request separately from the POST path and must report it the same. */
  @Test
  public void theStatusPathReportsAnUnusableBaseUrlTheSameWay() {
    OneD4Client client = clientFor("http://one_d4:8080");

    assertThatThrownBy(() -> client.status(java.util.UUID.randomUUID()))
        .isInstanceOf(OneD4Client.UpstreamException.class)
        .hasMessageContaining("not usable");
  }

  /**
   * A scheme the transport cannot send is the same class of fault. Worth its own case because the
   * host is fine here, so a guard that only looked at the host would let it through and leave the
   * failure to be relabelled somewhere downstream.
   */
  @Test
  public void reportsAnUnusableSchemeTheSameWay() {
    OneD4Client client = clientFor("ftp://one-d4:8080");

    assertThatThrownBy(() -> client.query(new QueryRequest("eco = \"B90\"", 10, 0)))
        .isInstanceOf(OneD4Client.UpstreamException.class)
        .hasMessageContaining("not usable");
  }

  /**
   * A URL this client cannot use must not take the process down. The chess.com tools share this
   * port and have no dependency on one_d4, mcpserver has no health gate in front of it, and {@code
   * restart: always} would turn a throw from this eagerly-built bean into a crash loop that 502s
   * every tool.
   */
  @Test
  public void anUnusableBaseUrlDoesNotPreventConstruction() {
    assertThat(clientFor("http://one_d4:8080").baseUrl()).isEqualTo("http://one_d4:8080");
  }

  /**
   * The addresses a deployment actually uses still work, including a public one — mcpserver may
   * legitimately be pointed at an external one_d4 rather than a container on its own network.
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
   * "Not reachable" alone does not say whether it was DNS, a refused connection or a reset, and the
   * cause is not rendered anywhere else a reader will see.
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
