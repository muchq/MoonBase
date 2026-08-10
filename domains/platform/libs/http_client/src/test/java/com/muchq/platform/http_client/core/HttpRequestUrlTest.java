package com.muchq.platform.http_client.core;

import static org.assertj.core.api.Assertions.assertThat;
import static org.assertj.core.api.Assertions.assertThatThrownBy;

import org.junit.jupiter.api.Test;

/**
 * What this builder will accept as a URL.
 *
 * <p>{@link java.net.URI#create} accepts strings the JDK's own HTTP client will not send, so a
 * request could be built successfully and then fail at execute() — one transport layer down from
 * the code that chose the string, where callers wrap failures as "the peer is down". Rejecting them
 * here puts the error next to the mistake.
 */
public class HttpRequestUrlTest {

  private static HttpRequest.Builder get(String url) {
    return HttpRequest.newBuilder().setMethod(HttpRequest.Method.GET).setUrl(url);
  }

  /**
   * The failure this test exists for. A Compose service name may contain an underscore and Docker's
   * DNS resolves it, but {@link java.net.URI} follows RFC 3986's reg-name rule and gives such an
   * authority a null host; {@code java.net.http.HttpRequest} then rejects the URI outright. Before
   * this guard the rejection surfaced from inside execute(), which is where clients report "not
   * reachable" — a claim about the network, made about a URL that never reached it.
   */
  @Test
  public void refusesAUrlWhoseHostJavaCannotParse() {
    assertThatThrownBy(() -> get("http://one_d4:8080/v1/query").build())
        .isInstanceOf(IllegalArgumentException.class)
        .hasMessageContaining("http://one_d4:8080/v1/query")
        .hasMessageContaining("host");
  }

  @Test
  public void refusesAUrlWithNoHostAtAll() {
    assertThatThrownBy(() -> get("http:///v1/query").build())
        .isInstanceOf(IllegalArgumentException.class)
        .hasMessageContaining("host");
  }

  /** A relative path has no host either, and this client has no base URL to resolve it against. */
  @Test
  public void refusesARelativeUrl() {
    assertThatThrownBy(() -> get("/v1/query").build())
        .isInstanceOf(IllegalArgumentException.class)
        .hasMessageContaining("host");
  }

  /**
   * Scheme is the same story one step along: the JDK client sends http and https and throws on
   * anything else, so a ws:// or file:// URL is a mistake this builder can name now rather than a
   * transport exception later.
   */
  @Test
  public void refusesASchemeTheTransportCannotSend() {
    assertThatThrownBy(() -> get("ftp://example.com/pub").build())
        .isInstanceOf(IllegalArgumentException.class)
        .hasMessageContaining("ftp");
  }

  /**
   * The guard rejects what the transport cannot use, and nothing else. Over-strict here would break
   * callers for whom the URL works perfectly.
   */
  @Test
  public void acceptsTheUrlsCallersActuallyBuild() {
    assertThat(get("https://api.chess.com/pub/player/hikaru").build().getUrl().getHost())
        .isEqualTo("api.chess.com");
    assertThat(get("http://one-d4:8080/v1/query").build().getUrl().getHost()).isEqualTo("one-d4");
    assertThat(get("http://localhost:34567/health").build().getUrl().getHost())
        .isEqualTo("localhost");
    assertThat(get("http://127.0.0.1:8080/health").build().getUrl().getHost())
        .isEqualTo("127.0.0.1");
    assertThat(get("http://[::1]:8080/health").build().getUrl().getHost()).isEqualTo("[::1]");
  }

  /**
   * A URL this builder accepts is one the JDK client will also accept. The two checks above are
   * this repo's restatement of the JDK's rules, and a restatement can drift; this asserts the
   * agreement directly rather than trusting that it holds.
   */
  @Test
  public void everyAcceptedUrlIsOneTheJdkClientWillTake() {
    for (String url :
        new String[] {
          "https://api.chess.com/pub/player/hikaru",
          "http://one-d4:8080/v1/query",
          "http://localhost:34567/health",
          "http://[::1]:8080/health"
        }) {
      HttpRequest request = get(url).build();
      assertThat(java.net.http.HttpRequest.newBuilder(request.getUrl()).GET().build().uri())
          .as(url)
          .isEqualTo(request.getUrl());
    }
  }
}
