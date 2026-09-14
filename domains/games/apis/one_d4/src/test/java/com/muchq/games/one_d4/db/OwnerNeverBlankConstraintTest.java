package com.muchq.games.one_d4.db;

import static org.assertj.core.api.Assertions.assertThat;
import static org.assertj.core.api.Assertions.assertThatCode;
import static org.assertj.core.api.Assertions.assertThatThrownBy;

import java.sql.Connection;
import java.sql.SQLException;
import java.util.List;
import java.util.UUID;
import org.junit.jupiter.api.BeforeEach;
import org.junit.jupiter.api.Test;

/**
 * A blank owner is refused by the request tables, whoever the writer is.
 *
 * <p>Every fence keys on {@code owner_id}: a heartbeat, a progress write and a terminal write all
 * match on it, and a claim spends an attempt only when the owner it presents differs from the one
 * stored. A row claimed under {@code ''} is one no worker's fence matches and one a re-claim under
 * {@code ''} does — so it never completes and never retires. {@code NULL} stays what it is: no
 * owner.
 */
public class OwnerNeverBlankConstraintTest {

  private static final List<String> REQUEST_TABLES =
      List.of("indexing_requests", "reanalysis_requests");

  private TestDb testDb;

  @BeforeEach
  public void setUp() {
    testDb = TestDb.create("ownerneverblank");
  }

  @Test
  public void everyRequestTableRefusesABlankOwner() throws Exception {
    for (String table : REQUEST_TABLES) {
      UUID id = insertRequest(table);
      assertThatThrownBy(() -> setOwner(table, id, ""))
          .as("%s should refuse a blank owner", table)
          .isInstanceOf(SQLException.class)
          .hasMessageContaining("owner_never_blank");
    }
  }

  /** The control: no owner and a named owner are both still storable. */
  @Test
  public void aNullOrNamedOwnerIsStillAccepted() throws Exception {
    for (String table : REQUEST_TABLES) {
      UUID id = insertRequest(table);
      assertThatCode(() -> setOwner(table, id, "cpp/host/1/9a1f"))
          .as("%s should accept a named owner", table)
          .doesNotThrowAnyException();
      assertThatCode(() -> setOwner(table, id, null))
          .as("%s should accept no owner", table)
          .doesNotThrowAnyException();
    }
  }

  /** The constraints are named, so {@code Migration.verify()} notices a database missing them. */
  @Test
  public void theConstraintsAreVisibleToBootVerification() {
    assertThat(constraintNames())
        .containsExactlyInAnyOrder(
            "indexing_requests_owner_never_blank", "reanalysis_requests_owner_never_blank");
  }

  /**
   * The normalise half. A database holding a blank before the constraint converges instead of
   * failing the deploy: the step nulls blanks only on the execution that adds the constraint.
   */
  @Test
  public void rerunningTheMigrationClearsABlankOwnerStoredBeforeTheConstraint() throws Exception {
    exec("ALTER TABLE indexing_requests DROP CONSTRAINT indexing_requests_owner_never_blank");
    UUID id = insertRequest("indexing_requests");
    setOwner("indexing_requests", id, "");

    new Migration(testDb.dataSource()).run();

    assertThat(ownerOf("indexing_requests", id)).isNull();
    assertThat(constraintNames()).contains("indexing_requests_owner_never_blank");
  }

  /** Re-run safe, like every step here: the file executes on every deploy. */
  @Test
  public void theStepIsIdempotent() {
    new Migration(testDb.dataSource()).run();
    new Migration(testDb.dataSource()).run();

    assertThat(constraintNames()).hasSize(REQUEST_TABLES.size());
  }

  /** A pending row on `table`. At most one reanalysis row per test: its live index allows one. */
  private UUID insertRequest(String table) throws SQLException {
    UUID id = UUID.randomUUID();
    String sql =
        switch (table) {
          case "indexing_requests" ->
              "INSERT INTO indexing_requests (id, player, platform, start_month, end_month)"
                  + " VALUES (?, ?, 'CHESS_COM', '2024-01', '2024-01')";
          case "reanalysis_requests" -> "INSERT INTO reanalysis_requests (id) VALUES (?)";
          default -> throw new IllegalArgumentException(table);
        };
    try (Connection conn = testDb.dataSource().getConnection();
        var stmt = conn.prepareStatement(sql)) {
      stmt.setObject(1, id);
      if (table.equals("indexing_requests")) {
        stmt.setString(2, UUID.randomUUID().toString());
      }
      stmt.executeUpdate();
    }
    return id;
  }

  private void setOwner(String table, UUID id, String owner) throws SQLException {
    try (Connection conn = testDb.dataSource().getConnection();
        var stmt = conn.prepareStatement("UPDATE " + table + " SET owner_id = ? WHERE id = ?")) {
      stmt.setString(1, owner);
      stmt.setObject(2, id);
      stmt.executeUpdate();
    }
  }

  private String ownerOf(String table, UUID id) throws SQLException {
    try (Connection conn = testDb.dataSource().getConnection();
        var stmt = conn.prepareStatement("SELECT owner_id FROM " + table + " WHERE id = ?")) {
      stmt.setObject(1, id);
      try (var rs = stmt.executeQuery()) {
        assertThat(rs.next()).as("row %s should still exist", id).isTrue();
        return rs.getString(1);
      }
    }
  }

  private void exec(String sql) throws SQLException {
    try (Connection conn = testDb.dataSource().getConnection();
        var stmt = conn.createStatement()) {
      stmt.execute(sql);
    }
  }

  private List<String> constraintNames() {
    try (Connection conn = testDb.dataSource().getConnection();
        var stmt =
            conn.prepareStatement(
                "SELECT conname FROM pg_constraint c JOIN pg_namespace n ON n.oid ="
                    + " c.connamespace WHERE n.nspname = ? AND conname LIKE"
                    + " '%owner_never_blank'")) {
      stmt.setString(1, testDb.schema());
      var names = new java.util.ArrayList<String>();
      try (var rs = stmt.executeQuery()) {
        while (rs.next()) {
          names.add(rs.getString(1));
        }
      }
      return names;
    } catch (SQLException e) {
      throw new RuntimeException(e);
    }
  }
}
