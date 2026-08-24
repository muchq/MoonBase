package com.muchq.games.one_d4.db;

import java.sql.Connection;
import java.sql.PreparedStatement;
import java.sql.ResultSet;
import java.sql.SQLException;
import java.sql.Statement;
import java.util.ArrayList;
import java.util.Comparator;
import java.util.HashSet;
import java.util.List;
import java.util.Set;
import java.util.UUID;
import javax.sql.DataSource;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

/**
 * The schema: the numbered, idempotent .sql files under {@code migrations/} (#1419), in manifest
 * order, resolved for this dialect's engine by {@link MigrationFiles} and split into statements by
 * {@link SqlStatements}. The files are the one copy of the DDL; this class carries none.
 *
 * <p>{@link #run} applies them — the standalone {@code one_d4_migrate} deploy step ({@link
 * MigrationRunner}), and the H2 test path, which has no deploy step in front of it. There is no
 * tracking table: re-running everything is the whole mechanism, which is what makes the two callers
 * interchangeable. See {@code migrations/README.md} for the authoring rules.
 *
 * <p>{@link #verify} checks they have already been applied, and writes nothing. That is what the
 * service runs at boot (#1426), so nothing but {@code one_d4_migrate} writes the deployed schema.
 *
 * <p>No transaction wraps {@link #run}, matching the statement-at-a-time behavior the DDL has
 * always had here: a failure stops at the failing step, leaves the earlier idempotent steps
 * applied, and names the step it died in.
 */
public class Migration {
  private static final Logger LOG = LoggerFactory.getLogger(Migration.class);

  /**
   * Everything a schema holds that a migration step can create, by name. Types are deliberately not
   * compared: both runners read one set of files, and a boot that refuses to serve is too blunt an
   * answer to a column somebody widened by hand.
   */
  private static final String OBJECTS_IN_SCHEMA =
      """
      SELECT 'table ' || table_name FROM information_schema.tables WHERE table_schema = ?
      UNION ALL
      SELECT 'column ' || table_name || '.' || column_name FROM information_schema.columns
        WHERE table_schema = ?
      UNION ALL
      SELECT 'index ' || indexname FROM pg_indexes WHERE schemaname = ?
      UNION ALL
      SELECT 'constraint ' || conname FROM pg_constraint c
        JOIN pg_namespace n ON n.oid = c.connamespace WHERE n.nspname = ?
      """;

  /** Enough of the list to act on; the count above it says how much was elided. */
  private static final int NAMED_IN_FAILURE = 20;

  /** Missing tables first, then the columns of tables that do exist, then what indexes them. */
  private static final List<String> KINDS = List.of("table", "column", "index", "constraint");

  private final DataSource dataSource;
  private final SqlDialect dialect;

  public Migration(DataSource dataSource, SqlDialect dialect) {
    this.dataSource = dataSource;
    this.dialect = dialect;
  }

  /**
   * What the service does about the schema when it starts: check it, or build it. Postgres has
   * {@code one_d4_migrate} in front of it and gets {@link #verify}; the H2 test path has nothing in
   * front of it and gets {@link #run}. Here rather than in {@code IndexerModule} so the choice is
   * reachable from a test that can watch what it wrote.
   */
  public void atBoot() {
    if (dialect.migratedBeforeBoot()) {
      verify();
    } else {
      run();
    }
  }

  public void run() {
    try (Connection conn = dataSource.getConnection();
        Statement stmt = conn.createStatement()) {
      apply(stmt);
      LOG.info("Database migration completed successfully");
    } catch (SQLException e) {
      throw new RuntimeException("Failed to run database migration", e);
    }
  }

  /**
   * Refuses to return if the deployed schema is missing anything the migrations create.
   *
   * <p>What to expect comes from the files rather than from a list anyone maintains: the migrations
   * run into a scratch schema, and what they build there is compared with what the service's own
   * schema holds. It happens inside a transaction that is always rolled back, so the scratch schema
   * is never committed and the live tables are never touched — every statement runs against the
   * empty copies.
   *
   * <p>Postgres only: it needs DDL to be transactional. The H2 path calls {@link #run} instead
   * ({@link SqlDialect#migratedBeforeBoot}).
   */
  public void verify() {
    String scratch = "one_d4_verify_" + UUID.randomUUID().toString().replace("-", "");
    try (Connection conn = dataSource.getConnection()) {
      boolean autoCommit = conn.getAutoCommit();
      conn.setAutoCommit(false);
      try {
        List<String> missing = missingFrom(conn, scratch);
        if (!missing.isEmpty()) {
          throw new IllegalStateException(
              "The database is missing "
                  + missing.size()
                  + " object(s) the migrations create, so one_d4_migrate has not completed against"
                  + " it: "
                  + String.join(
                      ", ", missing.subList(0, Math.min(missing.size(), NAMED_IN_FAILURE)))
                  + (missing.size() > NAMED_IN_FAILURE ? ", ..." : ""));
        }
      } finally {
        // Undoes the scratch schema and the search_path with it, whichever way this leaves.
        conn.rollback();
        conn.setAutoCommit(autoCommit);
      }
      LOG.info("Database schema verified against migrations/");
    } catch (SQLException e) {
      throw new RuntimeException("Failed to verify database schema", e);
    }
  }

  /** Objects the migrations build that the connection's own schema does not have, sorted. */
  private List<String> missingFrom(Connection conn, String scratch) throws SQLException {
    String live;
    try (Statement stmt = conn.createStatement();
        ResultSet rs = stmt.executeQuery("SELECT current_schema()")) {
      rs.next();
      live = rs.getString(1);
    }
    Set<String> present = objectsIn(conn, live);

    try (Statement stmt = conn.createStatement()) {
      stmt.execute("CREATE SCHEMA " + scratch);
    }
    setLocalSearchPath(conn, scratch);
    try (Statement stmt = conn.createStatement()) {
      apply(stmt);
    }

    List<String> missing = new ArrayList<>();
    for (String object : objectsIn(conn, scratch)) {
      if (!present.contains(object)) {
        missing.add(object);
      }
    }
    // A table nobody created is missing every column too, and listing them buries the one line
    // that says what to do about it.
    Set<String> absentTables = new HashSet<>();
    for (String object : missing) {
      if (object.startsWith("table ")) {
        absentTables.add(object.substring("table ".length()));
      }
    }
    missing.removeIf(
        object ->
            object.startsWith("column ")
                && absentTables.contains(
                    object.substring("column ".length(), object.lastIndexOf('.'))));
    missing.sort(
        Comparator.comparingInt(Migration::kindOf).thenComparing(Comparator.naturalOrder()));
    return missing;
  }

  private static int kindOf(String object) {
    return KINDS.indexOf(object.substring(0, object.indexOf(' ')));
  }

  private void apply(Statement stmt) throws SQLException {
    String engine = dialect.migrationsEngine();
    for (String step : MigrationFiles.steps()) {
      for (String sql : SqlStatements.split(MigrationFiles.sqlFor(step, engine))) {
        try {
          stmt.execute(sql);
        } catch (SQLException e) {
          throw new RuntimeException("Migration step " + step + " failed on " + engine, e);
        }
      }
    }
  }

  private static Set<String> objectsIn(Connection conn, String schema) throws SQLException {
    Set<String> objects = new HashSet<>();
    try (PreparedStatement stmt = conn.prepareStatement(OBJECTS_IN_SCHEMA)) {
      for (int i = 1; i <= 4; i++) {
        stmt.setString(i, schema);
      }
      try (ResultSet rs = stmt.executeQuery()) {
        while (rs.next()) {
          objects.add(rs.getString(1));
        }
      }
    }
    return objects;
  }

  /** Transaction-scoped, so the rollback that ends verify() puts the pooled connection back. */
  private static void setLocalSearchPath(Connection conn, String schema) throws SQLException {
    try (PreparedStatement stmt =
        conn.prepareStatement("SELECT set_config('search_path', ?, true)")) {
      stmt.setString(1, schema);
      stmt.execute();
    }
  }
}
