package com.muchq.games.one_d4.db;

import java.sql.Connection;
import java.sql.SQLException;
import java.sql.Statement;
import javax.sql.DataSource;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

/**
 * Applies the schema: the numbered, idempotent .sql files under {@code migrations/} (#1419), in
 * manifest order, resolved for this dialect's engine by {@link MigrationFiles} and split into
 * statements by {@link SqlStatements}. The files are the one copy of the DDL; this class carries
 * none.
 *
 * <p>Runs as the standalone {@code one_d4_migrate} deploy step ({@link MigrationRunner}) <em>and
 * then again</em> at service boot (an {@code IndexerModule} {@code @Context} bean) — the compose
 * gate serializes the two, since the statements are idempotent but not concurrency-safe. Re-running
 * everything is the whole mechanism: there is no tracking table. #1426 tracks demoting the
 * boot-time run to a verifier. See {@code migrations/README.md} for the authoring rules.
 *
 * <p>No transaction wraps the run, matching the statement-at-a-time behavior the DDL has always had
 * here: a failure stops at the failing step, leaves the earlier idempotent steps applied, and names
 * the step it died in.
 */
public class Migration {
  private static final Logger LOG = LoggerFactory.getLogger(Migration.class);

  private final DataSource dataSource;
  private final SqlDialect dialect;

  public Migration(DataSource dataSource, SqlDialect dialect) {
    this.dataSource = dataSource;
    this.dialect = dialect;
  }

  public void run() {
    String engine = dialect.migrationsEngine();
    try (Connection conn = dataSource.getConnection();
        Statement stmt = conn.createStatement()) {
      for (String step : MigrationFiles.steps()) {
        for (String sql : SqlStatements.split(MigrationFiles.sqlFor(step, engine))) {
          try {
            stmt.execute(sql);
          } catch (SQLException e) {
            throw new RuntimeException("Migration step " + step + " failed on " + engine, e);
          }
        }
      }
      LOG.info("Database migration completed successfully");
    } catch (SQLException e) {
      throw new RuntimeException("Failed to run database migration", e);
    }
  }
}
