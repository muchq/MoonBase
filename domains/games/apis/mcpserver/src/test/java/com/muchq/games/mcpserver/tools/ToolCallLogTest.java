package com.muchq.games.mcpserver.tools;

import static org.assertj.core.api.Assertions.assertThat;
import static org.assertj.core.api.Assertions.assertThatThrownBy;

import ch.qos.logback.classic.Level;
import ch.qos.logback.classic.Logger;
import ch.qos.logback.classic.spi.ILoggingEvent;
import ch.qos.logback.core.read.ListAppender;
import io.modelcontextprotocol.spec.McpSchema.CallToolResult;
import java.util.Objects;
import org.junit.jupiter.api.AfterEach;
import org.junit.jupiter.api.BeforeEach;
import org.junit.jupiter.api.Test;
import org.slf4j.LoggerFactory;
import org.slf4j.event.KeyValuePair;

public class ToolCallLogTest {

  private Logger logger;
  private ListAppender<ILoggingEvent> appender;

  @BeforeEach
  public void attachAppender() {
    logger = (Logger) LoggerFactory.getLogger(ToolCallLog.class);
    appender = new ListAppender<>();
    appender.start();
    logger.addAppender(appender);
  }

  @AfterEach
  public void detachAppender() {
    logger.detachAppender(appender);
  }

  static Object kv(ILoggingEvent event, String key) {
    for (KeyValuePair pair : Objects.requireNonNull(event.getKeyValuePairs())) {
      if (pair.key.equals(key)) {
        return pair.value;
      }
    }
    throw new AssertionError("no key-value pair " + key + " on " + event);
  }

  @Test
  public void aSuccessfulCallIsLoggedAtInfoAndItsResultReturnedUntouched() {
    CallToolResult result = ToolResults.text("payload");

    CallToolResult returned = ToolCallLog.logged("server_time", () -> result);

    assertThat(returned).isSameAs(result);
    assertThat(appender.list).hasSize(1);
    ILoggingEvent event = appender.list.get(0);
    assertThat(event.getLevel()).isEqualTo(Level.INFO);
    assertThat(event.getMessage()).isEqualTo("tool_call");
    assertThat(kv(event, "tool")).isEqualTo("server_time");
    assertThat(kv(event, "outcome")).isEqualTo("ok");
    assertThat(kv(event, "ms")).isInstanceOf(Long.class);
  }

  @Test
  public void aResultTheToolFlaggedAsAnErrorIsLoggedAtWarn() {
    ToolCallLog.logged("chess_com_games", () -> ToolResults.error("invalid month"));

    assertThat(appender.list).hasSize(1);
    ILoggingEvent event = appender.list.get(0);
    assertThat(event.getLevel()).isEqualTo(Level.WARN);
    assertThat(kv(event, "tool")).isEqualTo("chess_com_games");
    assertThat(kv(event, "outcome")).isEqualTo("error");
  }

  @Test
  public void anUncaughtExceptionIsLoggedAtErrorWithTheStackAndRethrown() {
    RuntimeException boom = new IllegalStateException("connection refused");

    assertThatThrownBy(
            () ->
                ToolCallLog.logged(
                    "index_chess_games",
                    () -> {
                      throw boom;
                    }))
        .isSameAs(boom);

    assertThat(appender.list).hasSize(1);
    ILoggingEvent event = appender.list.get(0);
    assertThat(event.getLevel()).isEqualTo(Level.ERROR);
    assertThat(kv(event, "tool")).isEqualTo("index_chess_games");
    assertThat(kv(event, "outcome")).isEqualTo("threw");
    assertThat(event.getThrowableProxy().getMessage()).isEqualTo("connection refused");
  }

  /**
   * The duration is measured around the call, not fabricated: a call that sleeps must report at
   * least that long. Loose on the upper side, since CI machines stall.
   */
  @Test
  public void theLoggedDurationCoversTheCall() {
    ToolCallLog.logged(
        "server_time",
        () -> {
          try {
            Thread.sleep(30);
          } catch (InterruptedException e) {
            Thread.currentThread().interrupt();
          }
          return ToolResults.text("x");
        });

    assertThat((Long) kv(appender.list.get(0), "ms")).isGreaterThanOrEqualTo(30);
  }
}
