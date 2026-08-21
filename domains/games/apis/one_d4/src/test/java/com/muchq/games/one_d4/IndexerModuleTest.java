package com.muchq.games.one_d4;

import static org.assertj.core.api.Assertions.assertThat;
import static org.assertj.core.api.Assertions.assertThatCode;
import static org.assertj.core.api.Assertions.assertThatThrownBy;

import java.io.InputStream;
import java.nio.charset.StandardCharsets;
import java.util.List;
import org.junit.jupiter.api.Test;

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
   * The dependency half of "H2 is test-only". Production code that merely avoids naming H2 is one
   * edit away from using it again with nothing to fail; a closure that cannot resolve the driver is
   * not. This target depends on the module and its db library and on no test database, so the
   * driver's absence here is its absence from one_d4's production dependency closure.
   *
   * <p>The control is pgjdbc, which the same closure does carry — without it this passes just as
   * well against a lookup that can no longer find anything at all.
   */
  @Test
  public void h2IsNotOnTheProductionClasspath() {
    assertThatThrownBy(() -> Class.forName("org.h2.Driver"))
        .as(
            "org.h2.Driver resolves from one_d4's production dependency closure. H2 is for tests;"
                + " a deployed container must not be able to fall back to an in-memory database.")
        .isInstanceOf(ClassNotFoundException.class);

    assertThatCode(() -> Class.forName("org.postgresql.Driver"))
        .as("pgjdbc is missing too, so the assertion above proves nothing")
        .doesNotThrowAnyException();
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

  /**
   * The production DAOs and Migration carry Postgres SQL only. H2's {@code MERGE INTO} is the
   * observable mark of the old in-class dialect branch; its absence here (with {@code ON CONFLICT}
   * present on {@link com.muchq.games.one_d4.db.PostgresSqlDialect} as the control) is what makes
   * "H2 is a test dialect" a fact about the class files rather than a comment.
   */
  @Test
  public void productionSqlCarriesNoH2Merge() throws Exception {
    for (Class<?> type :
        List.of(
            com.muchq.games.one_d4.db.GameFeatureDao.class,
            com.muchq.games.one_d4.db.IndexedPeriodDao.class,
            com.muchq.games.one_d4.db.Migration.class)) {
      String bytes = compiledBytes(type);
      assertThat(bytes).as("%s still names useH2", type.getSimpleName()).doesNotContain("useH2");
      assertThat(bytes)
          .as("%s still carries H2 MERGE INTO SQL", type.getSimpleName())
          .doesNotContain("MERGE INTO");
    }

    String postgres = compiledBytes(com.muchq.games.one_d4.db.PostgresSqlDialect.class);
    assertThat(postgres)
        .as("PostgresSqlDialect does not name ON CONFLICT, so the absences above prove nothing")
        .contains("ON CONFLICT");
  }

  @Test
  public void resolveJdbcUrl_prefersConfiguredProperty() {
    assertThat(IndexerModule.resolveJdbcUrl("  jdbc:h2:mem:x  ")).isEqualTo("jdbc:h2:mem:x");
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
