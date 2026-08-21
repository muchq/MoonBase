package com.muchq.games.one_d4.db;

import javax.sql.DataSource;
import org.jspecify.annotations.Nullable;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

/**
 * Applies the schema migrations and exits: the {@code one_d4_migrate} deploy step (#1419). Runs as
 * a one-shot compose service before {@code one_d4_worker} and {@code one_d4} start, so the C++
 * poller never races the Java service for a schema neither of them has created yet.
 *
 * <p>Same statements, same order, same code as the service's own boot-time {@link Migration} — the
 * two paths run in sequence (the compose gate serializes them) until #1426 demotes the boot-time
 * run to a verifier.
 *
 * <p>Reads {@code $INDEXER_DB_URL} (plus {@code $INDEXER_DB_USERNAME}/{@code $INDEXER_DB_PASSWORD})
 * exactly as the service does, and like the service it has no fallback: a migrate step that
 * "succeeds" against a database nobody deployed is worse than one that fails loudly, because
 * everything gated on it then starts against the wrong schema.
 *
 * <p>{@code main} is one delegating line so the exit code cannot depend on the pool's threads, and
 * {@link #run} carries the whole contract, where {@code MigrationRunnerTest} can reach it.
 */
public final class MigrationRunner {
  private static final Logger LOG = LoggerFactory.getLogger(MigrationRunner.class);

  private MigrationRunner() {}

  public static void main(String[] args) {
    System.exit(
        run(
            System.getenv("INDEXER_DB_URL"),
            System.getenv("INDEXER_DB_USERNAME"),
            System.getenv("INDEXER_DB_PASSWORD")));
  }

  /** The whole one-shot: 0 on a completed migration, 1 on anything else. */
  static int run(@Nullable String url, @Nullable String username, @Nullable String password) {
    if (url == null || url.isBlank()) {
      LOG.error(
          "INDEXER_DB_URL is not set. one_d4_migrate needs the same PostgreSQL JDBC URL the"
              + " service uses (jdbc:postgresql://host:5432/db).");
      return 1;
    }
    try {
      // Inside the try: an unreachable database fails pool construction, and that is this
      // container's likeliest failure, so it takes the same logged path as a failed statement.
      DataSource dataSource = DataSourceFactory.create(url.strip(), username, password);
      new Migration(dataSource, new PostgresSqlDialect()).run();
      return 0;
    } catch (RuntimeException e) {
      LOG.error("Migration failed", e);
      return 1;
    }
  }
}
