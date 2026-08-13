package com.muchq.games.one_d4;

import static org.assertj.core.api.Assertions.assertThat;

import ch.qos.logback.classic.Level;
import ch.qos.logback.classic.spi.ILoggingEvent;
import ch.qos.logback.core.read.ListAppender;
import com.muchq.games.one_d4.db.IndexingRequestStore;
import com.muchq.games.one_d4.queue.IndexQueue;
import com.muchq.games.one_d4.worker.IndexWorker;
import io.micronaut.context.annotation.Bean;
import java.io.InputStream;
import java.nio.charset.StandardCharsets;
import java.time.Clock;
import org.junit.jupiter.api.Test;
import org.slf4j.LoggerFactory;

public class IndexerModuleTest {

  /**
   * compose hands this container {@code jdbc:postgresql://one_d4_postgres:5432/one_d4} (#1351), and
   * that hostname has underscores in it. Three lines away in the same compose file sits the
   * opposite lesson — mcpserver must call {@code one-d4}, not {@code one_d4}, because {@link
   * java.net.URI} gives an authority containing an underscore a null host and every request built
   * from it fails — so the natural assumption is that this URL needs an alias too.
   *
   * <p>It does not: pgjdbc parses the URL with its own parser rather than through {@code URI}. That
   * is the whole reason the deploy can point at the service key directly instead of adding another
   * alias to carry forever, so it is pinned rather than trusted — a driver upgrade that tightened
   * host parsing would otherwise surface as one_d4 failing to reach its database on deploy.
   */
  @Test
  public void pgjdbcAcceptsTheUnderscoredHostnameComposeHandsUs() throws Exception {
    org.postgresql.Driver driver = new org.postgresql.Driver();
    String url = "jdbc:postgresql://one_d4_postgres:5432/one_d4?user=one_d4&password=secret";

    assertThat(driver.acceptsURL(url)).isTrue();
    assertThat(org.postgresql.Driver.parseURL(url, null))
        .as("the driver must resolve the underscored authority to a host, not drop it")
        .containsEntry("PGHOST", "one_d4_postgres")
        .containsEntry("PGDBNAME", "one_d4");
  }

  /**
   * The control. Without it the assertion above would hold just as well against a driver that
   * accepted everything, which is the failure mode that would let a libpq-shaped URL through.
   */
  @Test
  public void pgjdbcRejectsTheLibpqUrlShapeGolfHubUses() throws Exception {
    assertThat(new org.postgresql.Driver().acceptsURL("postgresql://one_d4_postgres:5432/one_d4"))
        .as("golf_hub's C++ form is not a JDBC URL and must not be mistaken for one")
        .isFalse();
  }

  /**
   * The poller is a daemon thread, so nothing stops it at shutdown unless the container is told to.
   * Without this annotation a deploy is indistinguishable from a crash: the in-flight row stays
   * owned by a process that no longer exists for a full lease, and the attempt it spent is gone.
   *
   * <p>Asserted on the factory method rather than by booting a context, because that is where the
   * mistake would be made — the method is easy to edit without noticing the annotation, and
   * IndexWorkerLifecycleTest already covers what stop() does once it is called. McpModuleTest boots
   * a real context and closes it, which is where the container's half of this is exercised.
   */
  @Test
  public void indexWorkerLifecycleIsToldToStopOnShutdown() throws Exception {
    Bean bean =
        IndexerModule.class
            .getMethod(
                "indexWorkerLifecycle",
                IndexQueue.class,
                IndexWorker.class,
                IndexingRequestStore.class,
                Clock.class)
            .getAnnotation(Bean.class);

    assertThat(bean).as("the lifecycle bean declares no preDestroy").isNotNull();
    assertThat(bean.preDestroy()).isEqualTo("stop");
  }

  @Test
  public void readJdbcUrl_returnsEnvVar_whenSet() {
    String result = IndexerModule.readJdbcUrl("jdbc:postgresql://prod:5432/db");
    assertThat(result).isEqualTo("jdbc:postgresql://prod:5432/db");
  }

  @Test
  public void readJdbcUrl_stripsEnvVar() {
    String result = IndexerModule.readJdbcUrl("  jdbc:postgresql://host/db  ");
    assertThat(result).isEqualTo("jdbc:postgresql://host/db");
  }

  /**
   * {@code INDEXER_DB_URL=} with nothing after it is what compose produces when the variable it
   * interpolates is absent from the host's environment, so this is the realistic misconfiguration
   * rather than a synthetic one. It has to resolve to H2 rather than reach Hikari as a blank URL,
   * which fails at pool construction with "No suitable driver".
   */
  @Test
  public void readJdbcUrl_ignoresBlankEnvVar() {
    String result = IndexerModule.readJdbcUrl("   ");
    assertThat(result).isEqualTo("jdbc:h2:mem:indexer;DB_CLOSE_DELAY=-1");
  }

  /**
   * The variable is now the entire resolution. There used to be a rank between it and H2 — {@code
   * /etc/one_d4/db_config}, read here until #1362 — so an unset variable landed on whatever that
   * file held. Nothing on disk can change this answer any more, which is why the method takes no
   * path: the only input is the environment.
   */
  @Test
  public void readJdbcUrl_returnsDefault_whenEnvVarIsUnset() {
    String result = IndexerModule.readJdbcUrl(null);
    assertThat(result).isEqualTo("jdbc:h2:mem:indexer;DB_CLOSE_DELAY=-1");
  }

  /**
   * The absence half of #1362, asserted where the behaviour lives rather than in {@code
   * deploy_config_test.go}: that test pins what compose hands the container, and would stay green
   * against a class that had quietly grown a second source. Nothing enforced "no file is read"
   * except the method signature, and a signature is a comment with a compiler.
   *
   * <p>Read off the compiled class because that is the only place the property is observable once
   * the parameter is gone. A file read leaves {@code java/nio/file/...} in the constant pool no
   * matter which method it hides in, which an assertion about parameter types would miss.
   *
   * <p>The control matters more than usual here: a scan that read nothing — wrong resource name,
   * empty stream — would report the absence just as confidently. Requiring the variable's own name
   * to be present proves the bytes are this class's, and pins the spelling {@code compose.yaml} has
   * to match at the same time.
   */
  @Test
  public void readJdbcUrl_consultsNoFile() throws Exception {
    String constantPool = compiledBytesOfIndexerModule();

    assertThat(constantPool)
        .as("the class does not name INDEXER_DB_URL, so these are not the bytes we meant to scan")
        .contains("INDEXER_DB_URL");
    assertThat(constantPool)
        .as(
            "IndexerModule references java.nio.file. Resolution is the environment and nothing"
                + " else since #1362 — a file fallback here is invisible to every other test,"
                + " including the compose guard.")
        .doesNotContain("java/nio/file");
  }

  /**
   * The fall-through to H2 is the operator's only signal that a deployed container is about to run
   * on a database that vanishes with the process — it starts, serves, and answers /health 200. It
   * was logged at INFO beside the file-not-found line it belonged to; with the file gone (#1362)
   * that INFO is the whole warning, sitting in the same stream as routine startup chatter.
   *
   * <p>Asserted rather than described because "we log this loudly" is exactly the kind of claim
   * that survives an edit that stops it being true.
   */
  @Test
  public void readJdbcUrl_warnsWhenItFallsBackToH2() {
    ch.qos.logback.classic.Logger logger =
        (ch.qos.logback.classic.Logger) LoggerFactory.getLogger(IndexerModule.class);
    ListAppender<ILoggingEvent> events = new ListAppender<>();
    events.start();
    logger.addAppender(events);
    try {
      IndexerModule.readJdbcUrl(null);
      IndexerModule.readJdbcUrl("   ");

      assertThat(events.list)
          .as("both ways of not setting the variable have to say so")
          .hasSize(2)
          .allSatisfy(
              event -> {
                assertThat(event.getLevel()).isEqualTo(Level.WARN);
                assertThat(event.getFormattedMessage()).contains("INDEXER_DB_URL");
              });

      // The twin. Without it this passes against a method that warns unconditionally, which is
      // the same as not warning at all once an operator learns to ignore it.
      events.list.clear();
      IndexerModule.readJdbcUrl("jdbc:postgresql://host/db");
      assertThat(events.list).as("a configured URL is not a misconfiguration").isEmpty();
    } finally {
      logger.detachAppender(events);
    }
  }

  /** The class's own bytes, decoded so that byte-for-byte substrings survive. */
  private static String compiledBytesOfIndexerModule() throws Exception {
    try (InputStream in = IndexerModule.class.getResourceAsStream("IndexerModule.class")) {
      assertThat(in).as("IndexerModule.class is not on the test classpath").isNotNull();
      return new String(in.readAllBytes(), StandardCharsets.ISO_8859_1);
    }
  }

  @Test
  public void parseThreads_returnsDefault_whenNull() {
    assertThat(IndexerModule.parseThreads(null, 4)).isEqualTo(4);
  }

  @Test
  public void parseThreads_returnsDefault_whenBlank() {
    assertThat(IndexerModule.parseThreads("   ", 4)).isEqualTo(4);
  }

  @Test
  public void parseThreads_returnsDefault_whenUnparseable() {
    assertThat(IndexerModule.parseThreads("abc", 4)).isEqualTo(4);
  }

  @Test
  public void parseThreads_returnsDefault_whenNonPositive() {
    assertThat(IndexerModule.parseThreads("0", 4)).isEqualTo(4);
    assertThat(IndexerModule.parseThreads("-3", 4)).isEqualTo(4);
  }

  @Test
  public void parseThreads_respectsValidValue() {
    assertThat(IndexerModule.parseThreads("8", 4)).isEqualTo(8);
    assertThat(IndexerModule.parseThreads(" 16 ", 4)).isEqualTo(16);
  }
}
