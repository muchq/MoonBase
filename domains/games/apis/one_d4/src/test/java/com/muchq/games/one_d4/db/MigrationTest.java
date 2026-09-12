package com.muchq.games.one_d4.db;

import static org.assertj.core.api.Assertions.assertThat;
import static org.assertj.core.api.Assertions.assertThatThrownBy;

import java.sql.Connection;
import java.sql.DatabaseMetaData;
import java.sql.ResultSet;
import java.sql.Statement;
import java.time.Instant;
import java.util.UUID;
import javax.sql.DataSource;
import org.junit.jupiter.api.BeforeEach;
import org.junit.jupiter.api.Test;

/**
 * What the migration files build, asserted against the schema they build it in. An empty schema per
 * test, because half of these are about the upgrade path onto a column rather than about a fresh
 * one.
 */
public class MigrationTest {

  private DataSource dataSource;
  private String schema;

  @BeforeEach
  public void setUp() {
    TestDb empty = TestDb.emptySchema("migration");
    dataSource = empty.dataSource();
    schema = empty.schema();
  }

  @Test
  public void run_createsMotifOccurrencesTable_andDropsHasMotifColumns() throws Exception {
    Migration migration = new Migration(dataSource);
    migration.run();

    try (Connection conn = dataSource.getConnection()) {
      DatabaseMetaData meta = conn.getMetaData();
      try (ResultSet tables =
          meta.getTables(null, schema, "motif_occurrences", new String[] {"TABLE"})) {
        assertThat(tables.next()).as("motif_occurrences table should exist").isTrue();
      }

      // has_* boolean columns should not exist — motif queries use motif_occurrences directly
      try (ResultSet columns = meta.getColumns(null, schema, "game_features", "has_pin")) {
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
    new Migration(dataSource).run();

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
    new Migration(dataSource).run();

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
   * Reanalysis is its own queue, deliberately (#1389 phase 5).
   *
   * <p>The indexers' {@code claimNext} is unfiltered over {@code indexing_requests}, so a
   * reanalysis row in that table is one an indexer would claim, find no player or months on, and
   * fail — spending an attempt on a job it cannot run, during a rolling deploy where some instances
   * have the filter and some do not. A second table is what keeps the two pollers from ever seeing
   * each other\'s work.
   *
   * <p>Every lease column is read as a primitive by the claim path, so one left nullable arrives
   * silently as "never attempted" rather than as an error.
   */
  @Test
  public void run_createsReanalysisRequestsWithItsOwnLeaseColumns() throws Exception {
    new Migration(dataSource).run();

    try (Connection conn = dataSource.getConnection()) {
      DatabaseMetaData meta = conn.getMetaData();
      try (ResultSet tables =
          meta.getTables(null, schema, "reanalysis_requests", new String[] {"TABLE"})) {
        assertThat(tables.next()).as("reanalysis_requests table should exist").isTrue();
      }

      for (String column :
          new String[] {
            "id",
            "status",
            "created_at",
            "updated_at",
            "owner_id",
            "lease_expires_at",
            "attempts",
            "error_message",
            "cursor_game_url",
            "games_processed",
            "games_failed"
          }) {
        try (ResultSet columns = meta.getColumns(null, schema, "reanalysis_requests", column)) {
          assertThat(columns.next()).as("reanalysis_requests.%s should exist", column).isTrue();
        }
      }
    }

    // The counters and the attempt budget are read as ints. A default-less column would hand the
    // claim path a zero it cannot distinguish from a real one.
    try (Connection conn = dataSource.getConnection();
        Statement stmt = conn.createStatement()) {
      stmt.execute("INSERT INTO reanalysis_requests (status) VALUES (\'PENDING\')");
      try (ResultSet rs =
          stmt.executeQuery(
              "SELECT attempts, games_processed, games_failed, cursor_game_url"
                  + " FROM reanalysis_requests")) {
        assertThat(rs.next()).isTrue();
        assertThat(rs.getObject("attempts")).as("attempts must not be NULL").isNotNull();
        assertThat(rs.getObject("games_processed"))
            .as("games_processed must not be NULL")
            .isNotNull();
        assertThat(rs.getObject("games_failed")).as("games_failed must not be NULL").isNotNull();
        assertThat(rs.getObject("cursor_game_url"))
            .as("a fresh pass has not finished a page yet, so its cursor starts unset")
            .isNull();
      }
    }
  }

  /**
   * At most one live reanalysis pass. Two PENDING rows are two claimable passes, and two worker
   * replicas would walk the whole corpus twice for no benefit — so the queue refuses the second at
   * insert. Existence only here; what the index actually rejects is driven in {@code
   * PostgresSingleLiveReanalysisTest}.
   */
  @Test
  public void run_addsTheSingleLiveReanalysisIndex() throws Exception {
    new Migration(dataSource).run();

    try (Connection conn = dataSource.getConnection();
        ResultSet indexes =
            conn.getMetaData().getIndexInfo(null, schema, "reanalysis_requests", false, false)) {
      boolean found = false;
      while (indexes.next()) {
        String name = indexes.getString("INDEX_NAME");
        if (name != null && name.equalsIgnoreCase("idx_reanalysis_requests_single_live")) {
          found = true;
        }
      }
      assertThat(found).as("idx_reanalysis_requests_single_live should exist").isTrue();
    }
  }

  /**
   * The index the poller's candidate scan depends on, on a query every instance runs constantly.
   */
  @Test
  public void run_addsTheClaimableIndex() throws Exception {
    new Migration(dataSource).run();

    try (Connection conn = dataSource.getConnection();
        ResultSet indexes =
            conn.getMetaData().getIndexInfo(null, schema, "indexing_requests", false, false)) {
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
    new Migration(dataSource).run();

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
            conn.getMetaData().getIndexInfo(null, schema, "game_features", false, false)) {
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
        .containsExactly("played_at:D", "game_url:A");
  }

  /**
   * The username indexes behind the player-participation guard, and the predicate they exist to
   * serve. Both halves in one test: the compiler must still emit the case-folded-on-both-sides
   * shape, and the indexes must still be {@code LOWER(...)} over the matching column. Either
   * drifting alone fails here rather than quietly costing the plan; the plan-level proof lives in
   * {@code PostgresPlayerIndexTest}.
   */
  @Test
  public void run_addsTheUsernameIndexesBehindTheParticipationGuard() throws Exception {
    new Migration(dataSource).run();

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

    // One index per side — an OR across two columns is served by two indexes, not one — and each
    // must fold its own side's column: an index pointed at the wrong column keeps the name this
    // test looks for while serving nothing.
    assertThat(indexDefinition("idx_game_features_white_username"))
        .contains("lower((white_username)::text)");
    assertThat(indexDefinition("idx_game_features_black_username"))
        .contains("lower((black_username)::text)");
  }

  /** {@code pg_indexes.indexdef}, which is where an expression index's expression is legible. */
  private String indexDefinition(String indexName) throws Exception {
    try (Connection conn = dataSource.getConnection();
        var ps =
            conn.prepareStatement(
                "SELECT indexdef FROM pg_indexes WHERE schemaname = ? AND indexname = ?")) {
      ps.setString(1, schema);
      ps.setString(2, indexName);
      try (ResultSet rs = ps.executeQuery()) {
        assertThat(rs.next()).as("%s does not exist", indexName).isTrue();
        return rs.getString(1);
      }
    }
  }

  /**
   * The retention delete's index, pinned to its column: {@code deleteOlderThan} filters {@code
   * game_features} on {@code indexed_at} hourly, and without this index a sweep truncated at its
   * 120s bound made no forward progress — same scan, next hour, forever. The plan-level proof on
   * the plan lives in {@code PostgresRetentionIndexTest}.
   */
  @Test
  public void run_addsTheIndexedAtRetentionIndex() throws Exception {
    new Migration(dataSource).run();

    java.util.List<String> columns = new java.util.ArrayList<>();
    try (Connection conn = dataSource.getConnection();
        ResultSet indexes =
            conn.getMetaData().getIndexInfo(null, schema, "game_features", false, false)) {
      while (indexes.next()) {
        String name = indexes.getString("INDEX_NAME");
        if (name != null && name.equalsIgnoreCase("idx_game_features_indexed_at")) {
          columns.add(indexes.getString("COLUMN_NAME").toLowerCase());
        }
      }
    }
    assertThat(columns).containsExactly("indexed_at");
  }

  @Test
  public void run_addsTitleAndOpeningColumns() throws Exception {
    Migration migration = new Migration(dataSource);
    migration.run();

    try (Connection conn = dataSource.getConnection()) {
      DatabaseMetaData meta = conn.getMetaData();
      for (String column :
          new String[] {"white_title", "black_title", "opening_name", "opening_family"}) {
        try (ResultSet columns = meta.getColumns(null, schema, "game_features", column)) {
          assertThat(columns.next()).as("game_features.%s column should exist", column).isTrue();
        }
      }
    }
  }

  @Test
  public void run_motifOccurrencesTableAcceptsInsertAndSelect() throws Exception {
    Migration migration = new Migration(dataSource);
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
   * The live-request invariant, as the schema states it since V018: one PENDING/PROCESSING row per
   * (player, platform, start_month, end_month, exclude_bullet), and terminal rows free to pile up.
   *
   * <p>Both statuses by name, not just the shape of the predicate. A predicate naming only one of
   * them still renders as a partial unique index over the right columns, and would leave the range
   * of a PROCESSING request open to a second live row — the #1249 race, reopened, since {@code
   * findLiveRequest} is a read and this index is the only thing closing it.
   */
  @Test
  public void run_addsTheLiveRequestIndexAsAPartialUniqueIndex() throws Exception {
    new Migration(dataSource).run();

    assertThat(indexDefinition("idx_indexing_requests_live"))
        .contains("UNIQUE")
        .contains("(player, platform, start_month, end_month, exclude_bullet)")
        .contains("WHERE ((status)::text = ANY")
        .contains("'PENDING'")
        .contains("'PROCESSING'");
  }

  /**
   * The same invariant driven rather than read: a PROCESSING incumbent holds its range against a
   * raw insert. PROCESSING specifically, because {@code createOrAdopt} short circuits on the live
   * row it finds and never reaches the index, so nothing else in the tree puts a second live row in
   * front of an in-flight one.
   */
  @Test
  public void theLiveRequestIndexRefusesASecondLiveRowAgainstAProcessingIncumbent()
      throws Exception {
    new Migration(dataSource).run();
    insertLegacyRequest("held", "2024-07", "2024-07", false, "PROCESSING", null);

    assertThatThrownBy(
            () -> insertLegacyRequest("held", "2024-07", "2024-07", false, "PENDING", null))
        .isInstanceOf(java.sql.SQLException.class)
        .hasMessageContaining("idx_indexing_requests_live");

    // The control: a terminal row for the same range is what the WHERE exists to allow.
    insertLegacyRequest("held", "2024-07", "2024-07", false, "COMPLETED", null);
  }

  /** V009's column and its constraint are gone, not merely unwritten. */
  @Test
  public void run_leavesNoDedupeKeyColumnBehind() throws Exception {
    new Migration(dataSource).run();

    try (Connection conn = dataSource.getConnection()) {
      try (ResultSet columns =
          conn.getMetaData().getColumns(null, schema, "indexing_requests", "dedupe_key")) {
        assertThat(columns.next()).as("dedupe_key survived V018").isFalse();
      }
    }
  }

  /**
   * The upgrade path onto the index, which is the one thing about V018 that a fresh schema cannot
   * show. V009's losers stayed PENDING while holding no key, so a real database can carry several
   * live rows per range — and {@code CREATE UNIQUE INDEX} would reject them and abort the deploy.
   *
   * <p>Staged by dropping the index and inserting the duplicates the old schema permitted, then
   * running the migrations again: the same statements a deploy would run, against the state a
   * deploy would find.
   *
   * <p>All three share a created_at, so the id tiebreak is what picks the survivor: without it the
   * statement retires nothing and {@code CREATE UNIQUE INDEX} aborts. Which row that is comes from
   * {@code findExistingRequest} rather than from a Java-side comparison — {@code UUID.compareTo} is
   * signed and Postgres compares uuid bytes unsigned.
   */
  @Test
  public void run_retiresPreExistingDuplicateLiveRequestsSoTheIndexCanBeBuilt() throws Exception {
    new Migration(dataSource).run();

    try (Connection conn = dataSource.getConnection();
        Statement stmt = conn.createStatement()) {
      stmt.execute("DROP INDEX idx_indexing_requests_live");
    }
    Instant sameInstant = Instant.parse("2026-06-01T00:00:00Z");
    UUID first = insertLegacyRequest("dupe", "2024-05", "2024-05", false, "PENDING", sameInstant);
    UUID second = insertLegacyRequest("dupe", "2024-05", "2024-05", false, "PENDING", sameInstant);
    UUID third =
        insertLegacyRequest("dupe", "2024-05", "2024-05", false, "PROCESSING", sameInstant);

    new Migration(dataSource).run();

    UUID adopted =
        new IndexingRequestDao(org.jdbi.v3.core.Jdbi.create(dataSource))
            .findExistingRequest("dupe", "CHESS_COM", "2024-05", "2024-05", false)
            .orElseThrow()
            .id();
    assertThat(statusOf(adopted))
        .as("the surviving row keeps the range")
        .isIn("PENDING", "PROCESSING");
    for (UUID id : java.util.List.of(first, second, third)) {
      if (!id.equals(adopted)) {
        assertThat(statusOf(id))
            .as("a superseded duplicate is retired, not left live")
            .isEqualTo("FAILED");
      }
    }

    // The point of the retirement: the index exists again afterwards, so the deploy completed.
    assertThat(indexDefinition("idx_indexing_requests_live")).contains("UNIQUE");
  }

  /**
   * Which duplicate survives when a worker is running one of them. Age alone would retire the
   * leased run in favour of an older idle row — clearing {@code owner_id} out from under a flush,
   * which no reclaim arm ever does — and the oldest row stops being the one that holds the range as
   * soon as a terminal write frees V009's key and a resubmit takes it.
   */
  @Test
  public void run_keepsTheDuplicateAWorkerIsRunning() throws Exception {
    new Migration(dataSource).run();

    try (Connection conn = dataSource.getConnection();
        Statement stmt = conn.createStatement()) {
      stmt.execute("DROP INDEX idx_indexing_requests_live");
    }
    UUID idle =
        insertLegacyRequest(
            "leased",
            "2024-06",
            "2024-06",
            false,
            "PENDING",
            Instant.parse("2026-01-01T00:00:00Z"));
    UUID running =
        insertLegacyRequest(
            "leased",
            "2024-06",
            "2024-06",
            false,
            "PROCESSING",
            Instant.parse("2026-02-01T00:00:00Z"));
    lease(running, Instant.parse("2099-01-01T00:00:00Z"));

    new Migration(dataSource).run();

    assertThat(statusOf(running))
        .as("the run a worker holds keeps the range")
        .isEqualTo("PROCESSING");
    assertThat(statusOf(idle))
        .as("the older idle duplicate is the one retired")
        .isEqualTo("FAILED");
  }

  /** Marks a request as held by a worker whose lease runs to {@code until}. */
  private void lease(UUID id, Instant until) throws Exception {
    try (Connection conn = dataSource.getConnection();
        var ps =
            conn.prepareStatement(
                "UPDATE indexing_requests SET owner_id = 'worker-1', lease_expires_at = ?"
                    + " WHERE id = ?")) {
      ps.setTimestamp(1, java.sql.Timestamp.from(until));
      ps.setObject(2, id);
      ps.executeUpdate();
    }
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

  private String statusOf(UUID id) {
    try (Connection conn = dataSource.getConnection();
        var ps = conn.prepareStatement("SELECT status FROM indexing_requests WHERE id = ?")) {
      ps.setObject(1, id);
      try (ResultSet rs = ps.executeQuery()) {
        assertThat(rs.next()).as("row %s is gone", id).isTrue();
        return rs.getString("status");
      }
    } catch (Exception e) {
      throw new RuntimeException(e);
    }
  }
}
