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
    // nanoTime, not currentTimeMillis: two tests starting in the same millisecond would silently
    // share a database — the same trap TestDb documents and avoids.
    String jdbcUrl = "jdbc:h2:mem:migration_" + System.nanoTime() + ";DB_CLOSE_DELAY=-1";
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

  /**
   * The columns that make the table dispatchable, and the upgrade path onto them.
   *
   * <p>Asserted against a table that already has a row, because the interesting case is not a fresh
   * schema — it is a request in flight during a deploy. Both columns are read as primitives, so a
   * row the migration left NULL would arrive silently as "do not skip the cache, never attempted".
   */
  @Test
  public void run_addsDispatchColumnsAndBackfillsExistingRows() throws Exception {
    new Migration(dataSource, true).run();

    UUID legacy = UUID.randomUUID();
    try (Connection conn = dataSource.getConnection();
        var ps =
            conn.prepareStatement(
                "INSERT INTO indexing_requests (id, player, platform, start_month, end_month,"
                    + " status) VALUES (?, 'legacy', 'CHESS_COM', '2024-01', '2024-01',"
                    + " 'PENDING')")) {
      ps.setObject(1, legacy);
      ps.executeUpdate();
    }

    // Idempotent: running again must not disturb the row or the columns.
    new Migration(dataSource, true).run();

    try (Connection conn = dataSource.getConnection();
        var ps =
            conn.prepareStatement(
                "SELECT skip_cache, attempts FROM indexing_requests WHERE id = ?")) {
      ps.setObject(1, legacy);
      try (ResultSet rs = ps.executeQuery()) {
        assertThat(rs.next()).isTrue();
        assertThat(rs.getObject("skip_cache")).as("skip_cache must not be NULL").isNotNull();
        assertThat(rs.getBoolean("skip_cache")).isFalse();
        assertThat(rs.getObject("attempts")).as("attempts must not be NULL").isNotNull();
        assertThat(rs.getInt("attempts")).isZero();
      }
    }
  }

  /**
   * The index the poller's candidate scan depends on, on a query every instance runs constantly.
   */
  @Test
  public void run_addsTheClaimableIndex() throws Exception {
    new Migration(dataSource, true).run();

    try (Connection conn = dataSource.getConnection();
        ResultSet indexes =
            conn.getMetaData().getIndexInfo(null, null, "INDEXING_REQUESTS", false, false)) {
      boolean found = false;
      while (indexes.next()) {
        String name = indexes.getString("INDEX_NAME");
        if (name != null && name.equalsIgnoreCase("idx_indexing_requests_claimable")) {
          found = true;
        }
      }
      assertThat(found).as("idx_indexing_requests_claimable should exist").isTrue();
    }
  }

  /**
   * The index behind the browse ordering — the {@code ORDER BY played_at DESC, game_url ASC LIMIT
   * n} that SqlCompiler appends to every query without an explicit ORDER BY. Column order and
   * directions are asserted, not just existence: an index on the same columns in the wrong order or
   * direction exists happily while the sort goes back to a full-table top-N.
   *
   * <p>Both sides of the contract are pinned here, in one test: what the compiler actually emits
   * for the browse default, and the index shape that serves it. Changing either alone fails this
   * test, instead of the index silently ceasing to satisfy the plan while a metadata-only assertion
   * stays green.
   */
  @Test
  public void run_addsThePlayedAtBrowseIndexMatchingTheCompilersOrderBy() throws Exception {
    new Migration(dataSource, true).run();

    String compiledDefault =
        new com.muchq.games.chessql.compiler.SqlCompiler()
            .compile(com.muchq.games.chessql.parser.Parser.parse("num.moves >= 0"), null)
            .selectSql();
    assertThat(compiledDefault)
        .as("the browse default's sort — the ORDER BY this index exists to serve")
        .endsWith("ORDER BY g.played_at DESC, g.game_url ASC");

    java.util.List<String> columnsInOrder = new java.util.ArrayList<>();
    try (Connection conn = dataSource.getConnection();
        ResultSet indexes =
            conn.getMetaData().getIndexInfo(null, null, "GAME_FEATURES", false, false)) {
      while (indexes.next()) {
        String name = indexes.getString("INDEX_NAME");
        if (name != null && name.equalsIgnoreCase("idx_game_features_played_at")) {
          columnsInOrder.add(
              indexes.getString("COLUMN_NAME") + ":" + indexes.getString("ASC_OR_DESC"));
        }
      }
    }

    assertThat(columnsInOrder)
        .as("idx_game_features_played_at must mirror ORDER BY played_at DESC, game_url ASC")
        .containsExactly("PLAYED_AT:D", "GAME_URL:A");
  }

  /**
   * The username indexes behind the player-participation guard, and the predicate they exist to
   * serve. On H2 they are plain column indexes (H2 has no expression indexes), so what this pins is
   * presence and the compiler's side of the contract: the guard must still be the
   * case-folded-on-both-sides shape the Postgres {@code LOWER(...)} expression indexes mirror. If
   * the compiler's predicate changes shape, this fails and points at the index definitions; the
   * plan-level proof on the deployment dialect lives in {@code PostgresPlayerIndexTest}.
   */
  @Test
  public void run_addsTheUsernameIndexesBehindTheParticipationGuard() throws Exception {
    new Migration(dataSource, true).run();

    String playerScoped =
        new com.muchq.games.chessql.compiler.SqlCompiler()
            .compile(com.muchq.games.chessql.parser.Parser.parse("outcome = \"win\""), "hikaru")
            .selectSql();
    assertThat(playerScoped)
        .as("the participation guard these indexes serve, case-folded on both sides")
        .contains("(LOWER(white_username) = LOWER(?) OR LOWER(black_username) = LOWER(?))");

    // The browse UI's username search reaches the same shape through a different compiler branch
    // (STRING_COLUMNS equality, no player) — the highest-traffic consumer of these indexes.
    String browseSearch =
        new com.muchq.games.chessql.compiler.SqlCompiler()
            .compile(
                com.muchq.games.chessql.parser.Parser.parse(
                    "white.username = \"hikaru\" OR black.username = \"hikaru\""),
                null)
            .selectSql();
    assertThat(browseSearch)
        .as("the browse search predicate these indexes serve, case-folded on both sides")
        .contains("(LOWER(white_username) = LOWER(?) OR LOWER(black_username) = LOWER(?))");

    java.util.Map<String, java.util.List<String>> columnsByIndex = new java.util.HashMap<>();
    try (Connection conn = dataSource.getConnection();
        ResultSet indexes =
            conn.getMetaData().getIndexInfo(null, null, "GAME_FEATURES", false, false)) {
      while (indexes.next()) {
        String name = indexes.getString("INDEX_NAME");
        if (name != null) {
          columnsByIndex
              .computeIfAbsent(name.toLowerCase(), k -> new java.util.ArrayList<>())
              .add(indexes.getString("COLUMN_NAME").toLowerCase());
        }
      }
    }
    // One index per side — an OR across two columns is served by two indexes, not one — and each
    // stand-in must sit on its own side's column: a stand-in redirected at the wrong column would
    // keep the name this test looks for while modeling an index Postgres doesn't have.
    assertThat(columnsByIndex)
        .containsEntry("idx_game_features_white_username", java.util.List.of("white_username"))
        .containsEntry("idx_game_features_black_username", java.util.List.of("black_username"));
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
            false,
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

  /**
   * The backfill decides which duplicate holds the slot; {@code findExistingRequest} decides which
   * one a later submit attaches to. They have to be the same row. Ordering by created_at alone
   * settles it only when the timestamps differ — and duplicate submits are precisely what produces
   * ties — so a tie is where the two can disagree and hand a caller a row nobody is working on
   * while the keyed row does the work.
   */
  @Test
  public void run_theBackfillWinnerIsTheRowASubsequentLookupReturns() throws Exception {
    new Migration(dataSource, true).run();

    Instant sameInstant = Instant.parse("2026-06-01T00:00:00Z");
    for (int i = 0; i < 3; i++) {
      insertLegacyRequest("tied", "2024-05", "2024-05", false, "PENDING", sameInstant);
    }
    clearDedupeKeys();

    new Migration(dataSource, true).run();

    IndexingRequestDao dao = new IndexingRequestDao(org.jdbi.v3.core.Jdbi.create(dataSource));
    UUID found =
        dao.findExistingRequest("tied", "CHESS_COM", "2024-05", "2024-05", false)
            .orElseThrow()
            .id();

    assertThat(dedupeKeyOf(found))
        .as("the row a submit attaches to must be the row that holds the slot")
        .isNotNull();
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
