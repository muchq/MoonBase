package com.muchq.games.one_d4;

import com.fasterxml.jackson.databind.ObjectMapper;
import com.muchq.games.chessql.compiler.SqlCompiler;
import com.muchq.games.one_d4.db.DataSourceFactory;
import com.muchq.games.one_d4.db.GameFeatureDao;
import com.muchq.games.one_d4.db.GameFeatureStore;
import com.muchq.games.one_d4.db.IndexedPeriodDao;
import com.muchq.games.one_d4.db.IndexedPeriodStore;
import com.muchq.games.one_d4.db.IndexingRequestDao;
import com.muchq.games.one_d4.db.IndexingRequestStore;
import com.muchq.games.one_d4.db.Migration;
import com.muchq.games.one_d4.db.PostgresSqlDialect;
import com.muchq.games.one_d4.db.ReanalysisRequestDao;
import com.muchq.games.one_d4.db.SqlDialect;
import com.muchq.games.one_d4.service.DataAvailabilityResolver;
import com.muchq.games.one_d4.service.IndexRequestService;
import com.muchq.platform.json.JsonUtils;
import io.micronaut.context.annotation.Context;
import io.micronaut.context.annotation.Factory;
import io.micronaut.context.annotation.Value;
import java.time.Clock;
import javax.sql.DataSource;
import org.jdbi.v3.core.Jdbi;
import org.jspecify.annotations.Nullable;

@Factory
public class IndexerModule {

  /**
   * The JDBC URL, from {@code $INDEXER_DB_URL}. The variable is the only source and there is no
   * default, so an unset or blank one is fatal.
   *
   * <p>Failing here is the point. Any fallback a service can start on unattended — an in-memory
   * database, a file on the host — turns a misconfigured URL into a container that boots, serves,
   * answers {@code /health} 200, and loses every write on restart. That is silent data loss where
   * an outage is the correct answer, and this exception is the outage.
   *
   * <p>Naming H2 as a default would not even reach that far: the driver is a test dependency and is
   * not on this classpath ({@code IndexerModuleTest.h2IsNotOnTheProductionClasspath}), so it would
   * fail at pool construction with "No suitable driver" instead of with the variable's name.
   *
   * <p>Local development needs a real Postgres and a real URL, the same as the deploy. See the
   * README.
   */
  static String readJdbcUrl() {
    return readJdbcUrl(System.getenv("INDEXER_DB_URL"));
  }

  static String readJdbcUrl(@Nullable String envUrl) {
    if (envUrl == null || envUrl.isBlank()) {
      throw new IllegalStateException(
          "INDEXER_DB_URL is not set. one_d4 needs a PostgreSQL JDBC URL"
              + " (jdbc:postgresql://host:5432/db); there is no in-memory fallback, because a"
              + " service that starts on one loses every write on restart without saying so.");
    }
    return envUrl.strip();
  }

  /** One clock for everything that stamps or compares retention timestamps. */
  @Context
  public Clock clock() {
    return Clock.systemUTC();
  }

  @Context
  public ObjectMapper objectMapper() {
    return JsonUtils.mapper();
  }

  /**
   * @param configuredUrl the {@code indexer.db.url} property. Tests set it to give each
   *     ApplicationContext its own H2 database, which is the only place H2 is reachable from — the
   *     driver is a test dependency. It has to be read here rather than assumed: a context whose
   *     property is ignored silently shares one database with every other context. Nothing sets it
   *     in production, where the URL comes from {@code $INDEXER_DB_URL}.
   */
  @Context
  @jakarta.inject.Named("indexerJdbcUrl")
  public String indexerJdbcUrl(@Value("${indexer.db.url:}") String configuredUrl) {
    return resolveJdbcUrl(configuredUrl);
  }

  @Context
  public DataSource dataSource(@jakarta.inject.Named("indexerJdbcUrl") String jdbcUrl) {
    return DataSourceFactory.create(
        jdbcUrl, System.getenv("INDEXER_DB_USERNAME"), System.getenv("INDEXER_DB_PASSWORD"));
  }

  /**
   * Production speaks Postgres only. Module-boot tests replace this bean via {@code
   * TestSqlDialectFactory}, which reads the same {@code indexerJdbcUrl} bean the DataSource uses so
   * the dialect cannot diverge from the pool.
   */
  @Context
  public SqlDialect sqlDialect() {
    return new PostgresSqlDialect();
  }

  /**
   * Property if set, otherwise {@code $INDEXER_DB_URL}. One resolution path for both the DataSource
   * and (in tests) the dialect factory.
   */
  static String resolveJdbcUrl(@Nullable String configuredUrl) {
    return configuredUrl == null || configuredUrl.isBlank() ? readJdbcUrl() : configuredUrl.strip();
  }

  @Context
  public Migration migration(DataSource dataSource, SqlDialect dialect) {
    Migration migration = new Migration(dataSource, dialect);
    migration.run();
    return migration;
  }

  @Context
  public Jdbi jdbi(DataSource dataSource) {
    return Jdbi.create(dataSource);
  }

  @Context
  public IndexingRequestStore indexingRequestStore(Jdbi jdbi, Clock clock) {
    return new IndexingRequestDao(jdbi, clock);
  }

  @Context
  public ReanalysisRequestDao reanalysisRequestDao(Jdbi jdbi) {
    return new ReanalysisRequestDao(jdbi);
  }

  @Context
  public GameFeatureStore gameFeatureStore(Jdbi jdbi, SqlDialect dialect) {
    return new GameFeatureDao(jdbi, dialect);
  }

  @Context
  public IndexedPeriodStore indexedPeriodStore(Jdbi jdbi, SqlDialect dialect) {
    return new IndexedPeriodDao(jdbi, dialect);
  }

  @Context
  public SqlCompiler sqlCompiler() {
    return new SqlCompiler();
  }

  // This JVM runs no chess: indexing and reanalysis are claimed off their
  // tables by the C++ one_d4_worker, analysis is one_d4_v2 (#1389).
  // Submitting a request is writing the row — the table is the queue
  // (#1279).
  @Context
  public IndexRequestService indexRequestService(
      IndexingRequestStore requestStore, DataAvailabilityResolver dataAvailability, Clock clock) {
    return new IndexRequestService(requestStore, dataAvailability, clock);
  }

  @Context
  public DataAvailabilityResolver dataAvailabilityResolver(IndexedPeriodStore periodStore) {
    return new DataAvailabilityResolver(periodStore);
  }
}
