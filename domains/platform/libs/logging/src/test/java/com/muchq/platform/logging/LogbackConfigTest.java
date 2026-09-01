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
  }
}
