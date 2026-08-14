package com.muchq.games.one_d4.worker;

import static org.assertj.core.api.Assertions.assertThat;

import com.muchq.games.chessql.compiler.SqlCompiler;
import com.muchq.games.chessql.parser.Parser;
import com.muchq.games.one_d4.api.dto.GameFeature;
import com.muchq.games.one_d4.db.GameFeatureDao;
import com.muchq.games.one_d4.db.H2SqlDialect;
import com.muchq.games.one_d4.db.IndexedPeriodDao;
import com.muchq.games.one_d4.db.IndexingRequestDao;
import com.muchq.games.one_d4.db.TestDb;
import java.time.Instant;
import java.time.temporal.ChronoUnit;
import java.util.List;
import java.util.UUID;
import javax.sql.DataSource;
import org.junit.jupiter.api.BeforeEach;
import org.junit.jupiter.api.Test;

public class RetentionWorkerTest {
  private GameFeatureDao dao;
  private IndexedPeriodDao periodDao;
  private IndexingRequestDao requestDao;
  private RetentionWorker worker;
  private UUID requestId;
  private DataSource dataSource;

  @BeforeEach
  public void setUp() {
    TestDb testDb = TestDb.create("retention");
    dataSource = testDb.dataSource();

    dao = new GameFeatureDao(testDb.jdbi(), H2SqlDialect.INSTANCE);
    periodDao = new IndexedPeriodDao(testDb.jdbi(), H2SqlDialect.INSTANCE);
    requestDao = new IndexingRequestDao(testDb.jdbi());
    worker = new RetentionWorker(dao, periodDao, requestDao);
    requestId = UUID.randomUUID();

    // Create a dummy indexing request to satisfy foreign key constraint
    try (var conn = dataSource.getConnection();
        var stmt =
            conn.prepareStatement(
                "INSERT INTO indexing_requests (id, player, platform, start_month, end_month,"
                    + " status) VALUES (?, 'p', 'CHESS_COM', '2024-01', '2024-01', 'COMPLETED')")) {
      stmt.setObject(1, requestId);
      stmt.executeUpdate();
    } catch (Exception e) {
      throw new RuntimeException(e);
    }
  }

  /**
   * The statement timeouts turned a wedged sweep from a hang into a throw, which only helps if the
   * schedule survives the throw: an exception escaping a {@code @Scheduled(fixedDelay)} task
   * cancels all future ticks permanently, so runRetention's catch-everything is the designed
   * recovery path — pinned here against stores that fail the way a timed-out statement now does
   * (every call throws; the datasource underneath is closed).
   */
  @Test
  public void runRetention_survivesStoreFailuresWithoutPropagating() throws Exception {
    TestDb broken = TestDb.create("retention_broken");
    RetentionWorker brokenWorker =
        new RetentionWorker(
            new GameFeatureDao(broken.jdbi(), H2SqlDialect.INSTANCE),
            new IndexedPeriodDao(broken.jdbi(), H2SqlDialect.INSTANCE),
            new IndexingRequestDao(broken.jdbi()));
    ((java.io.Closeable) broken.dataSource()).close();

    org.assertj.core.api.Assertions.assertThatCode(brokenWorker::runRetention)
        .as("a failing sweep must not escape and cancel the schedule")
        .doesNotThrowAnyException();

    // The control: the same worker method against healthy stores still does its work, so the
    // no-throw above is swallow-and-recover, not a sweep that never ran.
    GameFeature old = createGame("https://chess.com/control-old");
    dao.insertBatch(List.of(old));
    updateIndexedAt("https://chess.com/control-old", Instant.now().minus(8, ChronoUnit.DAYS));
    worker.runRetention();
    assertThat(countGames()).isEqualTo(0);
  }

  @Test
  public void runRetention_deletesOldGames() {
    // Insert a fresh game and an old game
    GameFeature fresh = createGame("https://chess.com/fresh");
    GameFeature old = createGame("https://chess.com/old");
    dao.insertBatch(List.of(fresh, old));
    updateIndexedAt("https://chess.com/old", Instant.now().minus(8, ChronoUnit.DAYS));

    assertThat(countGames()).isEqualTo(2);

    worker.runRetention();

    assertThat(countGames()).isEqualTo(1);
    List<GameFeature> remaining =
        dao.query(
            new SqlCompiler().compile(Parser.parse("game_url = \"https://chess.com/fresh\"")),
            10,
            0);
    assertThat(remaining).hasSize(1);
    assertThat(remaining.get(0).gameUrl()).isEqualTo("https://chess.com/fresh");
  }

  @Test
  public void runRetention_deletesOldPeriods() {
    periodDao.upsertPeriod("p", "CHESS_COM", "2024-01", Instant.now(), true, 5, false);
    periodDao.upsertPeriod("p", "CHESS_COM", "2024-02", Instant.now(), true, 5, false);
    updatePeriodFetchedAt("2024-02", Instant.now().minus(8, ChronoUnit.DAYS));

    assertThat(countPeriods()).isEqualTo(2);

    worker.runRetention();

    assertThat(countPeriods()).isEqualTo(1);
    assertThat(periodDao.findCompletePeriod("p", "CHESS_COM", "2024-01", false)).isPresent();
    assertThat(periodDao.findCompletePeriod("p", "CHESS_COM", "2024-02", false)).isEmpty();
  }

  /**
   * The acceptance case for #1266's foreign key constraint: a request old enough to sweep that
   * still owns games at the moment the pass starts. Games are deleted first, which is what lets the
   * request delete succeed — reverse the two and the FK refuses and the whole pass aborts.
   */
  @Test
  public void runRetention_deletesAnOldRequestAndItsGamesInForeignKeyOrder() {
    UUID oldRequest = insertRequest("COMPLETED", Instant.now().minus(40, ChronoUnit.DAYS));
    dao.insertBatch(List.of(createGame("https://chess.com/owned", oldRequest)));
    updateIndexedAt("https://chess.com/owned", Instant.now().minus(8, ChronoUnit.DAYS));

    assertThat(countGames()).isEqualTo(1);
    assertThat(countRequests()).isEqualTo(2);

    worker.runRetention();

    assertThat(countGames()).isEqualTo(0);
    assertThat(requestDao.findById(oldRequest)).isEmpty();
    // The fixture's own request is recent, so it stays.
    assertThat(requestDao.findById(requestId)).isPresent();
  }

  /**
   * The mirror case: the request has aged out but its games have not. Deleting it would violate the
   * foreign key, so it has to survive this pass and go on the next one after its games do.
   */
  @Test
  public void runRetention_keepsAnOldRequestWhoseGamesAreStillFresh() {
    UUID oldRequest = insertRequest("COMPLETED", Instant.now().minus(40, ChronoUnit.DAYS));
    dao.insertBatch(List.of(createGame("https://chess.com/fresh-game", oldRequest)));

    worker.runRetention();

    assertThat(countGames()).isEqualTo(1);
    assertThat(requestDao.findById(oldRequest)).isPresent();
  }

  /**
   * A request between the two windows — past the 7-day game clock, short of the 30-day request
   * clock — must survive. This is the gap in which the row is the only thing left that can tell a
   * user their data was pruned rather than never indexed, so sweeping requests on the games'
   * threshold would delete exactly the explanation the API is built to give.
   */
  @Test
  public void runRetention_keepsARequestThatIsPastTheGameWindowButNotTheRequestWindow() {
    UUID midLife = insertRequest("COMPLETED", Instant.now().minus(10, ChronoUnit.DAYS));

    worker.runRetention();

    assertThat(requestDao.findById(midLife)).isPresent();
  }

  @Test
  public void runRetention_retiresStrandedRequestsAndFreesTheirSlot() {
    UUID stranded = insertRequest("PENDING", Instant.now());
    // Exact rendering is IndexingRequestDao's business and is pinned in MigrationTest; here it
    // only has to be non-null so that clearing it is observable.
    setDedupeKey(stranded, "CHESS_COM|2024-01|2024-01|false|p2");
    updateRequestUpdatedAt(stranded, Instant.now().minus(6, ChronoUnit.HOURS));

    worker.runRetention();

    var retired = requestDao.findById(stranded).orElseThrow();
    assertThat(retired.status()).isEqualTo("FAILED");
    assertThat(retired.errorMessage()).contains("Abandoned");
    assertThat(dedupeKeyOf(stranded)).isNull();
  }

  @Test
  public void runRetention_leavesAFreshPendingRequestRunning() {
    UUID live = insertRequest("PENDING", Instant.now());

    worker.runRetention();

    assertThat(requestDao.findById(live).orElseThrow().status()).isEqualTo("PENDING");
  }

  private UUID insertRequest(String status, Instant createdAt) {
    UUID id = UUID.randomUUID();
    try (var conn = dataSource.getConnection();
        var ps =
            conn.prepareStatement(
                "INSERT INTO indexing_requests (id, player, platform, start_month, end_month,"
                    + " status, created_at, updated_at) VALUES (?, 'p2', 'CHESS_COM', '2024-01',"
                    + " '2024-01', ?, ?, ?)")) {
      ps.setObject(1, id);
      ps.setString(2, status);
      ps.setTimestamp(3, java.sql.Timestamp.from(createdAt));
      ps.setTimestamp(4, java.sql.Timestamp.from(createdAt));
      ps.executeUpdate();
    } catch (Exception e) {
      throw new RuntimeException(e);
    }
    return id;
  }

  private void setDedupeKey(UUID id, String key) {
    try (var conn = dataSource.getConnection();
        var ps =
            conn.prepareStatement("UPDATE indexing_requests SET dedupe_key = ? WHERE id = ?")) {
      ps.setString(1, key);
      ps.setObject(2, id);
      ps.executeUpdate();
    } catch (Exception e) {
      throw new RuntimeException(e);
    }
  }

  private String dedupeKeyOf(UUID id) {
    try (var conn = dataSource.getConnection();
        var ps = conn.prepareStatement("SELECT dedupe_key FROM indexing_requests WHERE id = ?")) {
      ps.setObject(1, id);
      try (var rs = ps.executeQuery()) {
        rs.next();
        return rs.getString("dedupe_key");
      }
    } catch (Exception e) {
      throw new RuntimeException(e);
    }
  }

  private void updateRequestUpdatedAt(UUID id, Instant updatedAt) {
    try (var conn = dataSource.getConnection();
        var ps =
            conn.prepareStatement("UPDATE indexing_requests SET updated_at = ? WHERE id = ?")) {
      ps.setTimestamp(1, java.sql.Timestamp.from(updatedAt));
      ps.setObject(2, id);
      ps.executeUpdate();
    } catch (Exception e) {
      throw new RuntimeException(e);
    }
  }

  private int countRequests() {
    try (var conn = dataSource.getConnection();
        var ps = conn.prepareStatement("SELECT COUNT(*) FROM indexing_requests");
        var rs = ps.executeQuery()) {
      rs.next();
      return rs.getInt(1);
    } catch (Exception e) {
      throw new RuntimeException(e);
    }
  }

  private void updateIndexedAt(String url, Instant indexedAt) {
    try (var conn = dataSource.getConnection();
        var ps =
            conn.prepareStatement("UPDATE game_features SET indexed_at = ? WHERE game_url = ?")) {
      ps.setTimestamp(1, java.sql.Timestamp.from(indexedAt));
      ps.setString(2, url);
      ps.executeUpdate();
    } catch (Exception e) {
      throw new RuntimeException(e);
    }
  }

  private void updatePeriodFetchedAt(String month, Instant fetchedAt) {
    try (var conn = dataSource.getConnection();
        var ps =
            conn.prepareStatement(
                "UPDATE indexed_periods SET fetched_at = ? WHERE year_month = ?")) {
      ps.setTimestamp(1, java.sql.Timestamp.from(fetchedAt));
      ps.setString(2, month);
      ps.executeUpdate();
    } catch (Exception e) {
      throw new RuntimeException(e);
    }
  }

  private int countGames() {
    try (var conn = dataSource.getConnection();
        var ps = conn.prepareStatement("SELECT COUNT(*) FROM game_features");
        var rs = ps.executeQuery()) {
      rs.next();
      return rs.getInt(1);
    } catch (Exception e) {
      throw new RuntimeException(e);
    }
  }

  private int countPeriods() {
    try (var conn = dataSource.getConnection();
        var ps = conn.prepareStatement("SELECT COUNT(*) FROM indexed_periods");
        var rs = ps.executeQuery()) {
      rs.next();
      return rs.getInt(1);
    } catch (Exception e) {
      throw new RuntimeException(e);
    }
  }

  private GameFeature createGame(String url) {
    return createGame(url, requestId);
  }

  private GameFeature createGame(String url, UUID owningRequest) {
    return new GameFeature(
        null,
        owningRequest,
        url,
        "CHESS_COM",
        "w",
        "b",
        1500,
        1500,
        null,
        null,
        "blitz",
        "B00",
        null,
        null,
        "1-0",
        Instant.now(),
        20,
        Instant.now(),
        "pgn");
  }
}
