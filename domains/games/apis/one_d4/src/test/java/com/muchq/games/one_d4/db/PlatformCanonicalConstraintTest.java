package com.muchq.games.one_d4.db;

import static org.assertj.core.api.Assertions.assertThat;
import static org.assertj.core.api.Assertions.assertThatCode;
import static org.assertj.core.api.Assertions.assertThatThrownBy;

import com.muchq.games.chessql.compiler.Platforms;
import java.sql.Connection;
import java.sql.SQLException;
import java.util.List;
import java.util.UUID;
import org.junit.jupiter.api.BeforeEach;
import org.junit.jupiter.api.Test;

/**
 * The stored spelling of platform is canonical, in the database rather than by convention.
 *
 * <p>ChessQL canonicalises a platform literal before binding it (#1539), so a row stored any other
 * way is unreachable by every spelling a user can type — the empty-corpus symptom that bug was
 * filed for, with the polarity flipped. Nothing enforced the premise: four tables held a bare
 * VARCHAR, and "every writer canonicalises" was true only because every writer happened to.
 */
public class PlatformCanonicalConstraintTest {

  /** Every table whose platform column keys a lookup, with the column that completes its row. */
  private static final List<String> PLATFORM_TABLES =
      List.of("indexing_requests", "game_features", "indexed_periods", "player_titles");

  private TestDb testDb;

  @BeforeEach
  public void setUp() {
    testDb = TestDb.create("platformcanonical");
  }

  @Test
  public void everyPlatformColumnRejectsANonCanonicalSpelling() {
    for (String table : PLATFORM_TABLES) {
      assertThatThrownBy(() -> insertWithPlatform(table, "chess.com"))
          .as("%s should refuse a dotted platform", table)
          .isInstanceOf(SQLException.class)
          .hasMessageContaining("platform_canonical");

      assertThatThrownBy(() -> insertWithPlatform(table, "chess_com"))
          .as("%s should refuse a lowercase platform", table)
          .isInstanceOf(SQLException.class)
          .hasMessageContaining("platform_canonical");

      // Upper-cased but still dotted: the case a rule that only upper-cased would let through,
      // and the one no ChessQL literal can ever match — canonical() turns every dot it is given
      // into an underscore, so nothing a user types reaches this row.
      assertThatThrownBy(() -> insertWithPlatform(table, "CHESS.COM"))
          .as("%s should refuse a dotted platform even upper-cased", table)
          .isInstanceOf(SQLException.class)
          .hasMessageContaining("platform_canonical");

      assertThatThrownBy(() -> insertWithPlatform(table, " CHESS_COM "))
          .as("%s should refuse an untrimmed platform", table)
          .isInstanceOf(SQLException.class)
          .hasMessageContaining("platform_canonical");

      // Tabs, not just spaces. Java's strip() takes both; Postgres btrim with
      // no character set takes only spaces, so a rule written that way accepts
      // this row and no query can ever reach it.
      assertThatThrownBy(() -> insertWithPlatform(table, "\tCHESS_COM\t"))
          .as("%s should refuse a tab-padded platform", table)
          .isInstanceOf(SQLException.class)
          .hasMessageContaining("platform_canonical");

      assertThatThrownBy(() -> insertWithPlatform(table, "\nCHESS_COM"))
          .as("%s should refuse a newline-padded platform", table)
          .isInstanceOf(SQLException.class)
          .hasMessageContaining("platform_canonical");

      assertThatThrownBy(() -> insertWithPlatform(table, ""))
          .as("%s should refuse an empty platform", table)
          .isInstanceOf(SQLException.class)
          .hasMessageContaining("platform_canonical");
    }
  }

  /** The control: the constraint refuses the wrong spellings without refusing the right ones. */
  @Test
  public void everyPlatformColumnAcceptsTheCanonicalSpellings() {
    for (String table : PLATFORM_TABLES) {
      assertThatCode(() -> insertWithPlatform(table, "CHESS_COM"))
          .as("%s should accept CHESS_COM", table)
          .doesNotThrowAnyException();
      assertThatCode(() -> insertWithPlatform(table, "LICHESS"))
          .as("%s should accept LICHESS", table)
          .doesNotThrowAnyException();
    }
  }

  /**
   * The two rules are one rule. A value Java calls canonical that Postgres refuses is an insert
   * that fails in production, so the agreement is checked rather than asserted in a comment —
   * including on the shapes the canonicaliser is there to fix.
   */
  @Test
  public void whateverPlatformsCanonicalReturnsSatisfiesTheConstraint() {
    List<String> inputs =
        List.of(
            "chess.com",
            "CHESS_COM",
            "  Chess.Com  ",
            "lichess",
            "LiChess",
            "chess24.com",
            "a.b.c",
            "\tchess.com\t",
            "\n LICHESS \r",
            "ALREADY_CANONICAL");

    for (String input : inputs) {
      String canonical = Platforms.canonical(input);
      assertThatCode(() -> insertWithPlatform("game_features", canonical))
          .as("Platforms.canonical(\"%s\") = \"%s\" must be storable", input, canonical)
          .doesNotThrowAnyException();
    }
  }

  /** The constraints are named, so {@code Migration.verify()} notices a database missing them. */
  @Test
  public void theConstraintsAreVisibleToBootVerification() {
    List<String> found = constraintNames();
    assertThat(found)
        .containsExactlyInAnyOrder(
            "indexing_requests_platform_canonical",
            "game_features_platform_canonical",
            "indexed_periods_platform_canonical",
            "player_titles_platform_canonical");
  }

  /**
   * The normalise half. A database that predates the constraint converges instead of failing the
   * deploy — the step runs its UPDATE only when it is about to add the constraint, so this is the
   * one execution that ever sees a non-canonical row.
   */
  @Test
  public void rerunningTheMigrationNormalizesARowStoredBeforeTheConstraint() throws Exception {
    exec("ALTER TABLE game_features DROP CONSTRAINT game_features_platform_canonical");
    String url = "https://chess.com/game/" + UUID.randomUUID();
    try (Connection conn = testDb.dataSource().getConnection();
        var stmt =
            conn.prepareStatement(
                "INSERT INTO game_features (request_id, game_url, platform) VALUES (?, ?,"
                    + " '  chess.com ')")) {
      stmt.setObject(1, seedRequest(conn));
      stmt.setString(2, url);
      stmt.executeUpdate();
    }

    new Migration(testDb.dataSource()).run();

    assertThat(platformOf(url)).isEqualTo("CHESS_COM");
    assertThat(constraintNames()).contains("game_features_platform_canonical");
  }

  /**
   * Re-run safe, like every step here: there is no tracking table, so this file executes on every
   * deploy. A second ADD CONSTRAINT would fail the deploy outright.
   */
  @Test
  public void theStepIsIdempotent() {
    new Migration(testDb.dataSource()).run();
    new Migration(testDb.dataSource()).run();

    assertThat(constraintNames()).hasSize(PLATFORM_TABLES.size());
  }

  private String platformOf(String gameUrl) {
    try (Connection conn = testDb.dataSource().getConnection();
        var stmt = conn.prepareStatement("SELECT platform FROM game_features WHERE game_url = ?")) {
      stmt.setString(1, gameUrl);
      try (var rs = stmt.executeQuery()) {
        assertThat(rs.next()).as("row %s should still exist", gameUrl).isTrue();
        return rs.getString(1);
      }
    } catch (SQLException e) {
      throw new RuntimeException(e);
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
                    + " '%platform_canonical'")) {
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

  /** A minimal valid row for each table, varying only the platform. */
  private void insertWithPlatform(String table, String platform) throws SQLException {
    try (Connection conn = testDb.dataSource().getConnection()) {
      switch (table) {
        case "indexing_requests" -> {
          try (var stmt =
              conn.prepareStatement(
                  "INSERT INTO indexing_requests (player, platform, start_month, end_month)"
                      + " VALUES (?, ?, '2024-01', '2024-01')")) {
            // A distinct player per row: idx_indexing_requests_live refuses a second live
            // request for the same range, which would mask the constraint under test.
            stmt.setString(1, UUID.randomUUID().toString());
            stmt.setString(2, platform);
            stmt.executeUpdate();
          }
        }
        case "game_features" -> {
          try (var stmt =
              conn.prepareStatement(
                  "INSERT INTO game_features (request_id, game_url, platform) VALUES (?, ?, ?)")) {
            stmt.setObject(1, seedRequest(conn));
            stmt.setString(2, "https://example.com/" + UUID.randomUUID());
            stmt.setString(3, platform);
            stmt.executeUpdate();
          }
        }
        case "indexed_periods" -> {
          try (var stmt =
              conn.prepareStatement(
                  "INSERT INTO indexed_periods (player, platform, year_month, fetched_at,"
                      + " is_complete, games_count) VALUES (?, ?, '2024-01', now(), TRUE, 0)")) {
            stmt.setString(1, UUID.randomUUID().toString());
            stmt.setString(2, platform);
            stmt.executeUpdate();
          }
        }
        case "player_titles" -> {
          try (var stmt =
              conn.prepareStatement(
                  "INSERT INTO player_titles (platform, username, title, observed_at, source)"
                      + " VALUES (?, ?, 'GM', now(), 'test')")) {
            stmt.setString(1, platform);
            stmt.setString(2, UUID.randomUUID().toString());
            stmt.executeUpdate();
          }
        }
        default -> throw new AssertionError("no fixture for " + table);
      }
    }
  }

  /** A parent request for game_features' foreign key, always canonical. */
  private UUID seedRequest(Connection conn) throws SQLException {
    UUID id = UUID.randomUUID();
    try (var stmt =
        conn.prepareStatement(
            "INSERT INTO indexing_requests (id, player, platform, start_month, end_month)"
                + " VALUES (?, ?, 'CHESS_COM', '2024-01', '2024-01')")) {
      stmt.setObject(1, id);
      stmt.setString(2, UUID.randomUUID().toString());
      stmt.executeUpdate();
    }
    return id;
  }
}
