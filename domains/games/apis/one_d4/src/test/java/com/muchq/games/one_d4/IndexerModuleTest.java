package com.muchq.games.one_d4;

import static org.assertj.core.api.Assertions.assertThat;
import static org.assertj.core.api.Assertions.assertThatThrownBy;

import com.muchq.games.one_d4.db.DataSourceFactory;
import com.muchq.games.one_d4.db.Migration;
import com.muchq.games.one_d4.db.PgTestUrls;
import io.micronaut.context.annotation.Context;
import java.io.InputStream;
import java.nio.charset.StandardCharsets;
import java.sql.Connection;
import java.sql.DriverManager;
import java.sql.ResultSet;
import java.sql.Statement;
import java.util.List;
import javax.sql.DataSource;
import org.junit.jupiter.api.Test;

public class IndexerModuleTest {

  /**
   * The retention windows are read at startup, not at the first request that needs one.
   *
   * <p>{@code RetentionPolicy} keeps them in static finals loaded from the classpath in its class
   * initializer, and every other reader is a method body — so without an eager bean touching the
   * class, a malformed {@code retention_policy.json} lets the container boot and answer {@code
   * /health} 200, then throws {@code ExceptionInInitializerError} on the first request that needed
   * a window and {@code NoClassDefFoundError}, with no cause attached, on every one after. That is
   * silent breakage where a failed startup is the correct answer, the same argument {@link
   * IndexerModule#readJdbcUrl} makes about its variable.
   *
   * <p>Asserted as a fact about the class file rather than about the returned value: a bean body of
   * {@code return Duration.ofDays(7);} touches nothing, initializes nothing, and would satisfy any
   * assertion comparing it to {@code RetentionPolicy.PERIOD} — because the test JVM evaluates that
   * expected value itself. The constant pool is what distinguishes reading the policy from
   * returning a number that happens to match it.
   */
  @Test
  public void theRetentionWindowsAreReadAtStartupRatherThanOnFirstUse() throws Exception {
    assertThat(IndexerModule.class.getMethod("retentionWindows").isAnnotationPresent(Context.class))
        .as("@Context is what makes this eager; without it the read moves to the first request")
        .isTrue();

    String constantPool = compiledBytesOfIndexerModule();
    assertThat(constantPool).as("not the bytes we meant to scan").contains("IndexRequestService");
    assertThat(constantPool)
        .as(
            "IndexerModule does not name RetentionPolicy, so nothing forces its class initializer"
                + " at startup and a broken retention_policy.json becomes a 500 per request"
                + " behind a healthy /health")
        .contains("RetentionPolicy");
  }

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
        .as("games_hub's C++ form is not a JDBC URL and must not be mistaken for one")
        .isFalse();
  }

  /**
   * This JVM runs no chess (#1389), as a fact about the class files: the module's constant pool
   * naming an extraction or worker type would mean a Java pipeline grew back without anyone
   * deciding it should.
   */
  @Test
  public void theModuleWiresNoExtractionAndNoIndexWorker() throws Exception {
    String constantPool = compiledBytesOfIndexerModule();

    assertThat(constantPool).as("not the bytes we meant to scan").contains("IndexRequestService");
    for (String retired :
        List.of(
            "FeatureExtractor", "MotifDetector", "IndexWorker", "PositionAnalyzer", "IndexQueue")) {
      assertThat(constantPool)
          .as(
              "IndexerModule names %s. Indexing, reanalysis and analysis are C++"
                  + " (one_d4_worker, one_d4_v2); a Java pipeline must not grow back by"
                  + " accident (#1389).",
              retired)
          .doesNotContain(retired);
    }
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
   * rather than a synthetic one — and it has to fail the same way a missing variable does.
   */
  @Test
  public void readJdbcUrl_refusesABlankEnvVar() {
    assertThatThrownBy(() -> IndexerModule.readJdbcUrl("   "))
        .isInstanceOf(IllegalStateException.class)
        .hasMessageContaining("INDEXER_DB_URL");
  }

  /**
   * A missing URL has to be fatal. Any default a service can start on unattended turns the
   * misconfiguration into a container that boots, serves, answers /health 200 and loses every write
   * on restart, and the message has to name the variable: a boot failure is the one place an
   * operator is guaranteed to look.
   */
  @Test
  public void readJdbcUrl_refusesAnUnsetEnvVar() {
    assertThatThrownBy(() -> IndexerModule.readJdbcUrl(null))
        .isInstanceOf(IllegalStateException.class)
        .hasMessageContaining("INDEXER_DB_URL");
  }

  /**
   * The environment is the only input to the URL. Asserted here rather than left to {@code
   * deploy_config_test.go}, which pins what compose hands the container and stays green against a
   * class that has grown a second source of its own.
   *
   * <p>Read off the compiled class because that is where the property is observable: a file read
   * leaves {@code java/nio/file/...} in the constant pool whichever method it hides in, while an
   * assertion about parameter types sees only the ones declared.
   *
   * <p>The control matters more than usual: a scan that read nothing — wrong resource name, empty
   * stream — reports the absence just as confidently. Requiring the variable's own name in the same
   * bytes proves they are this class's, and pins the spelling {@code compose.yaml} has to match.
   */
  @Test
  public void readJdbcUrl_consultsNoFile() throws Exception {
    String constantPool = compiledBytesOfIndexerModule();

    assertThat(constantPool)
        .as("the class does not name INDEXER_DB_URL, so these are not the bytes we meant to scan")
        .contains("INDEXER_DB_URL");
    assertThat(constantPool)
        .as(
            "IndexerModule references java.nio.file. The environment is the only input to the"
                + " URL; a file fallback here is invisible to every other test, including the"
                + " compose guard.")
        .doesNotContain("java/nio/file");
  }

  @Test
  public void resolveJdbcUrl_prefersConfiguredProperty() {
    assertThat(IndexerModule.resolveJdbcUrl("  jdbc:postgresql://db:5432/x  "))
        .isEqualTo("jdbc:postgresql://db:5432/x");
  }

  @Test
  public void resolveJdbcUrl_fallsThroughToEnvWhenPropertyBlank() {
    assertThatThrownBy(() -> IndexerModule.resolveJdbcUrl("   "))
        .isInstanceOf(IllegalStateException.class)
        .hasMessageContaining("INDEXER_DB_URL");
    assertThatThrownBy(() -> IndexerModule.resolveJdbcUrl(null))
        .isInstanceOf(IllegalStateException.class)
        .hasMessageContaining("INDEXER_DB_URL");
  }

  /**
   * The wiring, not just the method: the bean the service builds at startup checks the schema
   * rather than creating it. {@code one_d4_migrate} owns writing it (#1426), so a boot that built
   * what it found missing would be a second writer, and a container that started against a
   * half-migrated database would serve against it.
   *
   * <p>Against an empty schema, so the refusal and the absence are both observable — a migrated one
   * cannot tell {@link Migration#verify} from {@link Migration#run}.
   */
  @Test
  public void bootVerifiesTheSchemaRatherThanWritingIt() throws Exception {
    String rawUrl = PgTestUrls.requireRawUrl();
    String schema = "one_d4_module_boot";
    try (Connection conn = DriverManager.getConnection(PgTestUrls.jdbcUrl(rawUrl, null));
        Statement stmt = conn.createStatement()) {
      stmt.execute("DROP SCHEMA IF EXISTS " + schema + " CASCADE");
      stmt.execute("CREATE SCHEMA " + schema);
    }
    DataSource dataSource =
        DataSourceFactory.create(PgTestUrls.jdbcUrl(rawUrl, schema), null, null);

    assertThatThrownBy(() -> new IndexerModule().migration(dataSource))
        .as("boot returned against a schema one_d4_migrate has not written")
        .isInstanceOf(IllegalStateException.class);

    try (Connection conn = DriverManager.getConnection(PgTestUrls.jdbcUrl(rawUrl, null));
        Statement stmt = conn.createStatement();
        ResultSet rs =
            stmt.executeQuery(
                "SELECT count(*) FROM information_schema.tables WHERE table_schema = '"
                    + schema
                    + "'")) {
      rs.next();
      assertThat(rs.getInt(1))
          .as("boot built the schema instead of refusing to serve without it")
          .isZero();
    }
  }

  /** The class's own bytes, decoded so that byte-for-byte substrings survive. */
  private static String compiledBytesOfIndexerModule() throws Exception {
    return compiledBytes(IndexerModule.class);
  }

  private static String compiledBytes(Class<?> type) throws Exception {
    String resource = type.getSimpleName() + ".class";
    try (InputStream in = type.getResourceAsStream(resource)) {
      assertThat(in).as("%s is not on the test classpath", resource).isNotNull();
      return new String(in.readAllBytes(), StandardCharsets.ISO_8859_1);
    }
  }
}
