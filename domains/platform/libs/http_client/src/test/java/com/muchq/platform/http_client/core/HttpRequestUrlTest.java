package com.muchq.platform.http_client.core;

import static org.assertj.core.api.Assertions.assertThat;
import static org.assertj.core.api.Assertions.assertThatThrownBy;

import org.junit.jupiter.api.Test;

/**
 * What this builder will accept as a URL.
 *
 * <p>{@link java.net.URI#create} accepts strings the JDK's own HTTP client will not send, so a
 * request builds cleanly and then fails at execute() — one layer below the code that chose the
 * string, where callers wrap failures as a dead peer. Rejecting them here names the URL instead.
 */
public class HttpRequestUrlTest {

  private static HttpRequest.Builder get(String url) {
    return HttpRequest.newBuilder().setMethod(HttpRequest.Method.GET).setUrl(url);
  }

  /**
   * A Compose service name may contain an underscore and Docker's DNS resolves it, but {@link
   * java.net.URI} follows RFC 3986's reg-name rule and gives such an authority a null host, so
   * {@code java.net.http.HttpRequest} rejects the URI outright.
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
   * This builder and the JDK client agree, in both directions, about every URL below.
   *
   * <p>The guard is a restatement of {@code java.net.http}'s rules and can drift either way. Too
   * loose accepts a URL that fails one layer down, where callers report it as a dead peer; too
   * strict refuses one the transport would have sent. Walking only the URLs the builder accepts
   * cannot catch the second, because an over-strict guard just shrinks the set being walked.
   */
  @Test
  public void thisBuilderAndTheJdkClientAgreeOnEveryUrl() {
    for (String url :
        new String[] {
          "https://api.chess.com/pub/player/hikaru",
          "http://one-d4:8080/v1/query",
          "http://localhost:34567/health",
          "http://127.0.0.1:8080/health",
          "http://[::1]:8080/health",
          "http://user:pw@example.com/x",
          "HTTP://one-d4:8080/v1/query",
          "HTTPS://api.chess.com/pub",
          "http://one_d4:8080/v1/query",
          "http:///v1/query",
          "/v1/query",
          "ftp://example.com/pub",
          "ws://example.com/socket"
        }) {
      assertThat(buildAccepts(url)).as(url).isEqualTo(jdkAccepts(url));
    }
  }

  private static boolean buildAccepts(String url) {
    try {
      get(url).build();
      return true;
    } catch (IllegalArgumentException e) {
      return false;
    }
  }

  private static boolean jdkAccepts(String url) {
    try {
      java.net.http.HttpRequest.newBuilder(java.net.URI.create(url)).GET().build();
      return true;
    } catch (IllegalArgumentException e) {
      return false;
    }
  }
}
