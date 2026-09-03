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
          .addKeyValue("fields", "eco,white.elo")
          .addKeyValue("motifs", java.util.List.of("fork"))
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

    // The parameterized case is what services actually write. "message"
    // is the raw template and "arguments" the values — what machine
    // readers key on — and "formattedMessage" is the rendered line, for
    // anyone reading the stream by eye.
    JsonNode parameterized = lineContaining(captured.toString(UTF_8), "widget {} failed");
    assertThat(parameterized.get("message").asText()).isEqualTo("widget {} failed after {} tries");
    assertThat(parameterized.get("arguments").get(0).asText()).isEqualTo("w-7");
    assertThat(parameterized.get("arguments").get(1).asText()).isEqualTo("3");
    assertThat(parameterized.get("formattedMessage").asText())
        .isEqualTo("widget w-7 failed after 3 tries");

    // An ERROR with a throwable — the line Sentry and any alerting reads —
    // stays one parseable object with the exception structured inside it.
    JsonNode error = lineContaining(captured.toString(UTF_8), "boom");
    assertThat(error.get("level").asText()).isEqualTo("ERROR");
    assertThat(error.get("throwable").toString()).contains("connection refused");

    // Key-value pairs from the fluent API — how one_d4's query event carries
    // its fields (#1465) — land as a list of one-entry objects, every value
    // rendered as a string: a long, a comma-joined string (what one_d4
    // sends), and a List (which it deliberately does not, since this is
    // what a List looks like) all arrive the same way. The stats pipeline
    // reads exactly this shape, and selects the lines by loggerName.
    JsonNode kvp = lineContaining(captured.toString(UTF_8), "query_event");
    assertThat(kvp.get("kvpList").toString())
        .isEqualTo(
            "[{\"event\":\"query\"},{\"duration_us\":\"1234\"},"
                + "{\"fields\":\"eco,white.elo\"},{\"motifs\":\"[fork]\"}]");
    assertThat(kvp.get("loggerName").asText()).isEqualTo("com.muchq.some.Service");
  }

  private JsonNode lineContaining(String output, String needle) throws Exception {
    for (String candidate : output.split("\n")) {
      if (candidate.contains(needle)) {
        return new ObjectMapper().readTree(candidate);
      }
    }
    throw new AssertionError("no line containing " + needle);
  }

  /**
   * one_d4's query events (#1465) also go to a file of their own, rolled under the name shape
   * log_shipper recognizes; the directory is the deployment's to set. The console keeps a copy.
   */
  @Test
  public void theQueryEventLoggerWritesItsOwnRolledFile() throws Exception {
    java.nio.file.Path dir = java.nio.file.Files.createTempDirectory("query_events");
    ByteArrayOutputStream captured = new ByteArrayOutputStream();
    PrintStream original = System.out;
    LoggerContext context = new LoggerContext();
    System.setProperty("QUERY_EVENT_LOG_DIR", dir.toString());
    try {
      System.setOut(new PrintStream(captured, true, UTF_8));
      context.setMDCAdapter(new ch.qos.logback.classic.util.LogbackMDCAdapter());
      JoranConfigurator configurator = new JoranConfigurator();
      configurator.setContext(context);
      configurator.doConfigure(getClass().getClassLoader().getResource("logback.xml"));
      context
          .getLogger("com.muchq.games.one_d4.query_event")
          .atInfo()
          .addKeyValue("entry", "query")
          .addKeyValue("source", "ui")
          .log("query_event");
      context.getLogger("com.muchq.some.Service").info("not a query event");
    } finally {
      System.setOut(original);
      System.clearProperty("QUERY_EVENT_LOG_DIR");
      context.stop();
    }

    String file = java.nio.file.Files.readString(dir.resolve("query_events.log"), UTF_8);
    assertThat(file.strip().split("\n")).as("only the event logger's lines").hasSize(1);
    JsonNode event = new ObjectMapper().readTree(file.strip());
    assertThat(event.get("loggerName").asText()).isEqualTo("com.muchq.games.one_d4.query_event");
    assertThat(event.get("kvpList").toString())
        .isEqualTo("[{\"entry\":\"query\"},{\"source\":\"ui\"}]");
    assertThat(captured.toString(UTF_8)).as("the console keeps a copy").contains("query_event");

    // The roll name, rendered by logback's own pattern engine for a fixed hour, is the literal
    // log_shipper's test ships (TestALogbackHourlyRollShipsUnderItsDate): the two ends live in
    // different languages, so the shared spelling is the contract.
    String config =
        new String(
            getClass().getClassLoader().getResourceAsStream("logback.xml").readAllBytes(), UTF_8);
    java.util.regex.Matcher pattern =
        java.util.regex.Pattern.compile("<fileNamePattern>[^<]*/([^<]*)</fileNamePattern>")
            .matcher(config);
    assertThat(pattern.find()).isTrue();
    LoggerContext renderer = new LoggerContext();
    String rendered =
        new ch.qos.logback.core.rolling.helper.FileNamePattern(pattern.group(1), renderer)
            .convert(
                java.util.Date.from(
                    java.time.ZonedDateTime.of(
                            2026, 9, 1, 14, 30, 0, 0, java.time.ZoneId.systemDefault())
                        .toInstant()));
    assertThat(rendered).isEqualTo("query_events-2026-09-01T14.log.gz");
  }
}
