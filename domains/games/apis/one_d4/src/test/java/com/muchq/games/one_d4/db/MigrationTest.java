package com.muchq.games.one_d4.db;

import static org.assertj.core.api.Assertions.assertThat;

import java.sql.Connection;
import java.sql.DatabaseMetaData;
import java.sql.ResultSet;
import java.sql.Statement;
import java.time.Instant;
import java.util.UUID;
import javax.sql.DataSource;
import org.junit.jupiter.api.BeforeEach;
import org.junit.jupiter.api.Test;

public class MigrationTest {

  private DataSource dataSource;

  @BeforeEach
  public void setUp() {
    String jdbcUrl = "jdbc:h2:mem:migration_" + System.currentTimeMillis() + ";DB_CLOSE_DELAY=-1";
    dataSource = DataSourceFactory.create(jdbcUrl);
  }

  @Test
  public void run_createsMotifOccurrencesTable_andDropsHasMotifColumns() throws Exception {
    Migration migration = new Migration(dataSource, true);
    migration.run();

    try (Connection conn = dataSource.getConnection()) {
      DatabaseMetaData meta = conn.getMetaData();
      try (ResultSet tables =
          meta.getTables(null, null, "MOTIF_OCCURRENCES", new String[] {"TABLE"})) {
        assertThat(tables.next()).as("motif_occurrences table should exist").isTrue();
      }

      // has_* boolean columns should not exist — motif queries use motif_occurrences directly
      try (ResultSet columns = meta.getColumns(null, null, "GAME_FEATURES", "HAS_PIN")) {
        assertThat(columns.next()).as("game_features.has_pin column should not exist").isFalse();
      }
    }
  }

  @Test
  public void run_addsTitleAndOpeningColumns() throws Exception {
    Migration migration = new Migration(dataSource, true);
    migration.run();

    try (Connection conn = dataSource.getConnection()) {
      DatabaseMetaData meta = conn.getMetaData();
      for (String column :
          new String[] {"WHITE_TITLE", "BLACK_TITLE", "OPENING_NAME", "OPENING_FAMILY"}) {
        try (ResultSet columns = meta.getColumns(null, null, "GAME_FEATURES", column)) {
          assertThat(columns.next()).as("game_features.%s column should exist", column).isTrue();
        }
      }
    }
  }

  @Test
  public void run_motifOccurrencesTableAcceptsInsertAndSelect() throws Exception {
    Migration migration = new Migration(dataSource, true);
    migration.run();

    UUID requestId = UUID.randomUUID();
    String gameUrl = "https://chess.com/game/migration-test";

    try (Connection conn = dataSource.getConnection();
        Statement stmt = conn.createStatement()) {
      stmt.execute(
          "INSERT INTO indexing_requests (id, player, platform, start_month, end_month, status)"
              + " VALUES ('"
              + requestId
              + "', 'p', 'CHESS_COM', '2024-01', '2024-01', 'COMPLETED')");
      stmt.execute(
          "INSERT INTO game_features (request_id, game_url, platform, num_moves, indexed_at)"
              + " VALUES ('"
              + requestId
              + "', '"
              + gameUrl
              + "', 'CHESS_COM', 10, now())");
      stmt.execute(
          "INSERT INTO motif_occurrences (id, game_url, motif, ply, side, move_number, description)"
              + " VALUES ('"
              + UUID.randomUUID()
              + "', '"
              + gameUrl
              + "', 'CHECK', 5, 'white', 3, 'Check at move 3')");
    }

    try (Connection conn = dataSource.getConnection();
        Statement stmt = conn.createStatement();
        ResultSet rs =
            stmt.executeQuery(
                "SELECT game_url, motif, move_number, description FROM motif_occurrences"
                    + " WHERE game_url = '"
                    + gameUrl
                    + "'")) {
      assertThat(rs.next()).isTrue();
      assertThat(rs.getString("game_url")).isEqualTo(gameUrl);
      assertThat(rs.getString("motif")).isEqualTo("CHECK");
      assertThat(rs.getInt("move_number")).isEqualTo(3);
      assertThat(rs.getString("description")).isEqualTo("Check at move 3");
      assertThat(rs.next()).isFalse();
    }
  }

  /**
   * The backfill renders the dedupe key in SQL while {@link IndexingRequestDao} renders it in Java,
   * and the two have to agree exactly or a migrated row is invisible to dedupe. H2 spells a BOOLEAN
   * 'TRUE' where Java and Postgres spell it 'true', so the excludeBullet=true case is the one that
   * catches a missing LOWER(CAST(...)) — the false case passes either way.
   */
  @Test
  public void run_backfillsDedupeKeyMatchingTheJavaRendering() throws Exception {
    new Migration(dataSource, true).run();

    UUID pending = insertLegacyRequest("hikaru", "2024-01", "2024-03", true, "PENDING");
    UUID completed = insertLegacyRequest("magnus", "2024-01", "2024-01", false, "COMPLETED");
    clearDedupeKeys();

    new Migration(dataSource, true).run();

    assertThat(dedupeKeyOf(pending))
        .isEqualTo(IndexingRequestDao.dedupeKey("hikaru", "CHESS_COM", "2024-01", "2024-03", true));
    assertThat(dedupeKeyOf(completed)).as("terminal rows hold no slot").isNull();

    // The proof that the rendering agrees: a fresh submit for the same tuple adopts the migrated
    // row rather than trying to create a second one.
    IndexingRequestDao dao = new IndexingRequestDao(org.jdbi.v3.core.Jdbi.create(dataSource));
    IndexingRequestStore.Claim claim =
        dao.createOrAdopt(
            "hikaru",
            "CHESS_COM",
            "2024-01",
            "2024-03",
            true,
            java.time.Duration.ofHours(1),
            java.time.Instant.now());
    assertThat(claim.created()).isFalse();
    assertThat(claim.request().id()).isEqualTo(pending);
  }

  /**
   * The pre-constraint schema allowed several live rows per tuple, so the backfill has to key at
   * most one of them or ADD CONSTRAINT fails and the whole migration aborts.
   *
   * <p>All three rows are given the <em>same</em> created_at deliberately. The backfill orders
   * candidates by (created_at, id); on distinct timestamps created_at alone decides and the id
   * tiebreak never runs, so a fixture with distinct timestamps would pass against a backfill that
   * omitted it. A tie is what forces the tiebreak to do the work — and a tie is exactly what a
   * coarse clock produces when duplicate submits land in the same instant, which is the situation
   * that created these duplicates in the first place.
   */
  @Test
  public void run_backfillKeysOnlyOneOfSeveralLiveRequestsCreatedInTheSameInstant()
      throws Exception {
    new Migration(dataSource, true).run();

    Instant sameInstant = Instant.parse("2026-06-01T00:00:00Z");
    UUID first = insertLegacyRequest("dupe", "2024-05", "2024-05", false, "PENDING", sameInstant);
    UUID second = insertLegacyRequest("dupe", "2024-05", "2024-05", false, "PENDING", sameInstant);
    UUID third =
        insertLegacyRequest("dupe", "2024-05", "2024-05", false, "PROCESSING", sameInstant);
    clearDedupeKeys();

    new Migration(dataSource, true).run();

    long keyed =
        java.util.stream.Stream.of(first, second, third)
            .filter(id -> dedupeKeyOf(id) != null)
            .count();
    assertThat(keyed).as("exactly one duplicate may hold the slot").isEqualTo(1);
  }

  private UUID insertLegacyRequest(
      String player, String startMonth, String endMonth, boolean excludeBullet, String status)
      throws Exception {
    return insertLegacyRequest(player, startMonth, endMonth, excludeBullet, status, null);
  }

  private UUID insertLegacyRequest(
      String player,
      String startMonth,
      String endMonth,
      boolean excludeBullet,
      String status,
      Instant createdAt)
      throws Exception {
    UUID id = UUID.randomUUID();
    try (Connection conn = dataSource.getConnection();
        var ps =
            conn.prepareStatement(
                "INSERT INTO indexing_requests (id, player, platform, start_month, end_month,"
                    + " exclude_bullet, status, created_at) VALUES (?, ?, 'CHESS_COM', ?, ?, ?, ?,"
                    + " ?)")) {
      ps.setObject(1, id);
      ps.setString(2, player);
      ps.setString(3, startMonth);
      ps.setString(4, endMonth);
      ps.setBoolean(5, excludeBullet);
      ps.setString(6, status);
      ps.setTimestamp(7, java.sql.Timestamp.from(createdAt == null ? Instant.now() : createdAt));
      ps.executeUpdate();
    }
    return id;
  }

  /** Simulates rows written before the column existed, so the backfill has work to do. */
  private void clearDedupeKeys() throws Exception {
    try (Connection conn = dataSource.getConnection();
        Statement stmt = conn.createStatement()) {
      stmt.execute("UPDATE indexing_requests SET dedupe_key = NULL");
    }
  }

  private String dedupeKeyOf(UUID id) {
    try (Connection conn = dataSource.getConnection();
        var ps = conn.prepareStatement("SELECT dedupe_key FROM indexing_requests WHERE id = ?")) {
      ps.setObject(1, id);
      try (ResultSet rs = ps.executeQuery()) {
        assertThat(rs.next()).isTrue();
        return rs.getString("dedupe_key");
      }
    } catch (Exception e) {
      throw new RuntimeException(e);
    }
  }
}
