package com.muchq.games.one_d4.api;

import static org.assertj.core.api.Assertions.assertThat;

import io.micronaut.context.ApplicationContext;
import io.micronaut.runtime.server.EmbeddedServer;
import java.util.Map;
import org.junit.jupiter.api.AfterEach;
import org.junit.jupiter.api.BeforeEach;
import org.junit.jupiter.api.Test;

/**
 * Pins the wiring the unit tests cannot: that {@link FirstPageWarmer}'s {@code @Scheduled} method
 * actually fires in a real Micronaut context, so the cache is warm before the first request
 * arrives. If the annotation, the bean scope, or the scheduling processor dependency is lost, this
 * is the test that notices — the unit tests all call {@code refresh()} by hand.
 */
public class FirstPageWarmupTest {

  private EmbeddedServer server;

  @BeforeEach
  public void setUp() {
    server =
        ApplicationContext.run(
            EmbeddedServer.class,
            Map.of(
                "indexer.db.url",
                "jdbc:h2:mem:first_page_warmup_test_" + System.nanoTime() + ";DB_CLOSE_DELAY=-1",
                "micronaut.server.port",
                "-1"));
  }

  @AfterEach
  public void tearDown() {
    server.stop();
  }

  @Test
  public void cacheIsWarmedShortlyAfterStartupWithoutAnyRequest() throws Exception {
    FirstPageCache cache = server.getApplicationContext().getBean(FirstPageCache.class);

    // The warmer's initial delay is 1s; give a loaded CI machine plenty of headroom.
    long deadline = System.nanoTime() + java.time.Duration.ofSeconds(30).toNanos();
    while (cache.get().isEmpty() && System.nanoTime() < deadline) {
      Thread.sleep(100);
    }

    assertThat(cache.get())
        .as("the scheduled warmer populates the cache with no traffic")
        .isPresent();
    // Empty database, so the warmed snapshot is an empty page — warm and empty, not absent.
    assertThat(cache.get().orElseThrow().count()).isEqualTo(0);
  }
}
