package com.muchq.platform.logging;

import static java.nio.charset.StandardCharsets.UTF_8;
import static org.assertj.core.api.Assertions.assertThat;

import ch.qos.logback.classic.Logger;
import ch.qos.logback.classic.LoggerContext;
import ch.qos.logback.classic.joran.JoranConfigurator;
import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.ObjectMapper;
import java.io.ByteArrayOutputStream;
import java.io.PrintStream;
import java.time.Instant;
import org.junit.jupiter.api.Test;

/**
 * Boots the shared logback.xml — the one config every Java service ships — and reads what its
 * console appender actually writes. The stats pipeline parses these lines offline (#1459), so the
 * contract is structural: one JSON object per line, carrying an absolute timestamp. Before #1456
 * the pattern rendered bare {@code HH:mm:ss} wall-clock time, which two days of shipped logs cannot
 * even order.
 */
public class LogbackConfigTest {

  @Test
  public void theSharedConfigEmitsOneParseableJsonObjectPerLineWithAnAbsoluteTimestamp()
      throws Exception {
    ByteArrayOutputStream captured = new ByteArrayOutputStream();
    PrintStream original = System.out;
    LoggerContext context = new LoggerContext();
    try {
      // Before configuration: ConsoleAppender binds System.out at start.
      System.setOut(new PrintStream(captured, true, UTF_8));
      // A hand-built context has no MDC adapter (slf4j installs one on the
      // default context); appending NPEs without it.
      context.setMDCAdapter(new ch.qos.logback.classic.util.LogbackMDCAdapter());
      JoranConfigurator configurator = new JoranConfigurator();
      configurator.setContext(context);
      configurator.doConfigure(getClass().getClassLoader().getResource("logback.xml"));
      Logger logger = context.getLogger("com.muchq.some.Service");
      logger.info("hello structured world");
      logger.info("widget {} failed after {} tries", "w-7", 3);
      logger.error("boom", new IllegalStateException("connection refused"));
      logger
          .atInfo()
          .addKeyValue("event", "query")
          .addKeyValue("duration_us", 1234L)
          .addKeyValue("fields", java.util.List.of("eco", "white.elo"))
          .log("query_event");
    } finally {
      System.setOut(original);
      context.stop();
    }

    String line = null;
    for (String candidate : captured.toString(UTF_8).split("\n")) {
      if (candidate.contains("hello structured world")) {
        line = candidate;
      }
    }
    if (line == null) {
      ch.qos.logback.core.util.StatusPrinter.print(context);
    }
    assertThat(line).as("the logged line reached the console appender").isNotNull();

    JsonNode node = new ObjectMapper().readTree(line);
    assertThat(node.get("message").asText()).isEqualTo("hello structured world");
    assertThat(node.get("level").asText()).isEqualTo("INFO");
    assertThat(node.get("loggerName").asText()).isEqualTo("com.muchq.some.Service");
    // Epoch millis: absolute, order-preserving across days — the property
    // the old date-less pattern lacked.
    assertThat(node.get("timestamp").asLong())
        .isBetween(
            Instant.parse("2026-01-01T00:00:00Z").toEpochMilli(),
            Instant.parse("2100-01-01T00:00:00Z").toEpochMilli());

    // The parameterized case is what services actually write, and
    // JsonEncoder's contract there is NOT "message is the rendered text":
    // the raw template rides in "message" and the values in "arguments".
    // Anything reading these lines offline has to know that, so the
    // semantic is pinned, not discovered.
    JsonNode parameterized = lineContaining(captured.toString(UTF_8), "widget {} failed");
    assertThat(parameterized.get("message").asText()).isEqualTo("widget {} failed after {} tries");
    assertThat(parameterized.get("arguments").get(0).asText()).isEqualTo("w-7");
    assertThat(parameterized.get("arguments").get(1).asText()).isEqualTo("3");

    // An ERROR with a throwable — the line Sentry and any alerting reads —
    // stays one parseable object with the exception structured inside it.
    JsonNode error = lineContaining(captured.toString(UTF_8), "boom");
    assertThat(error.get("level").asText()).isEqualTo("ERROR");
    assertThat(error.get("throwable").toString()).contains("connection refused");

    // Key-value pairs from the fluent API — how one_d4's query event carries
    // its fields (#1465) — land as a list of one-entry objects, every value
    // rendered as a string. The stats pipeline reads exactly this shape.
    JsonNode kvp = lineContaining(captured.toString(UTF_8), "query_event");
    assertThat(kvp.get("kvpList").toString())
        .isEqualTo(
            "[{\"event\":\"query\"},{\"duration_us\":\"1234\"},"
                + "{\"fields\":\"[eco, white.elo]\"}]");
  }

  private JsonNode lineContaining(String output, String needle) throws Exception {
    for (String candidate : output.split("\n")) {
      if (candidate.contains(needle)) {
        return new ObjectMapper().readTree(candidate);
      }
    }
    throw new AssertionError("no line containing " + needle);
  }
}
