package com.muchq.games.mcpserver.tools;

import static org.assertj.core.api.Assertions.assertThat;

import org.junit.jupiter.api.Test;

/**
 * The envelope itself, at the edges the tools reach through it.
 *
 * <p>The per-tool suites cover the ordinary paths. What they cannot reach is a rejection carrying
 * no message, which arrives whenever a tool passes an exception's message straight through and that
 * message is null — {@code IndexerFacade} does exactly that, and its own null-propagating helper
 * makes it reachable rather than theoretical.
 */
public class ToolResultsTest {

  /**
   * A rejection with no message must still be a rejection.
   *
   * <p>{@code Map.of} rejects null values, so this used to throw on the way into the result — and a
   * throw from a tool becomes a JSON-RPC error, meaning the one rejection that most needs the
   * isError channel was the one that could not use it. Reported as a protocol failure rather than a
   * tool's answer, which is the confusion #1331 set out to end.
   */
  @Test
  public void aRejectionWithoutAMessageStillTravelsOnTheErrorChannel() {
    var result = ToolResults.error(null);

    assertThat(result.isError()).as("a missing message does not make it a success").isTrue();
    assertThat(ToolResultText.textOf(result))
        .as("and the caller gets a readable body rather than a null or a stack trace")
        .isEqualTo("{\"error\":\"unknown error\"}");
  }

  /** The ordinary case, so the above is not the only thing pinning the body's shape. */
  @Test
  public void aRejectionCarriesItsMessageInTheBodyAndTheFlag() {
    var result = ToolResults.error("invalid month: 13");

    assertThat(result.isError()).isTrue();
    assertThat(ToolResultText.textOf(result)).isEqualTo("{\"error\":\"invalid month: 13\"}");
  }
}
