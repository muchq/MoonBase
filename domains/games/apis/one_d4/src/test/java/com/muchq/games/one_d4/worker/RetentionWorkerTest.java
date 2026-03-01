package com.muchq.games.one_d4.worker;

import static org.assertj.core.api.Assertions.assertThat;

import com.muchq.games.chessql.compiler.SqlCompiler;
import com.muchq.games.chessql.parser.Parser;
import com.muchq.games.one_d4.api.dto.GameFeature;
import com.muchq.games.one_d4.db.DataSourceFactory;
import com.muchq.games.one_d4.db.GameFeatureDao;
import com.muchq.games.one_d4.db.IndexedPeriodDao;
import com.muchq.games.one_d4.db.Migration;
import java.time.Instant;
import java.time.temporal.ChronoUnit;
import java.util.List;
import java.util.UUID;
import javax.sql.DataSource;
import org.junit.Before;
import org.junit.Test;

public class RetentionWorkerTest {
  private GameFeatureDao dao;
  private IndexedPeriodDao periodDao;
  private RetentionWorker worker;
  private UUID requestId;
  private DataSource dataSource;

  @Before
  public void setUp() {
    String jdbcUrl = "jdbc:h2:mem:retention_" + System.currentTimeMillis() + ";DB_CLOSE_DELAY=-1";
    dataSource = DataSourceFactory.create(jdbcUrl, "sa", "");
    Migration migration = new Migration(dataSource, true);
    migration.run();

    dao = new GameFeatureDao(dataSource, true);
    periodDao = new IndexedPeriodDao(dataSource, true);
    worker = new RetentionWorker(dao, periodDao);
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

  @Test
  public void runRetention_deletesOldGames() {
    // Insert a fresh game
    GameFeature fresh = createGame("https://chess.com/fresh");
    dao.insert(fresh);

    // Insert an old game manually by bypassing DAO to set indexed_at
    dao.insert(createGame("https://chess.com/old"));
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
    periodDao.upsertPeriod("p", "CHESS_COM", "2024-01", Instant.now(), true, 5);
    periodDao.upsertPeriod("p", "CHESS_COM", "2024-02", Instant.now(), true, 5);
    updatePeriodFetchedAt("2024-02", Instant.now().minus(8, ChronoUnit.DAYS));

    assertThat(countPeriods()).isEqualTo(2);

    worker.runRetention();

    assertThat(countPeriods()).isEqualTo(1);
    assertThat(periodDao.findCompletePeriod("p", "CHESS_COM", "2024-01")).isPresent();
    assertThat(periodDao.findCompletePeriod("p", "CHESS_COM", "2024-02")).isEmpty();
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
    return new GameFeature(
        null,
        requestId,
        url,
        "CHESS_COM",
        "w",
        "b",
        1500,
        1500,
        "blitz",
        "B00",
        "1-0",
        Instant.now(),
        20,
        false, // hasPin
        false, // hasCrossPin
        false, // hasFork
        false, // hasSkewer
        false, // hasDiscoveredAttack
        false, // hasDiscoveredMate
        false, // hasDiscoveredCheck
        false, // hasCheck
        false, // hasCheckmate
        false, // hasPromotion
        false, // hasPromotionWithCheck
        false, // hasPromotionWithCheckmate
        false, // hasBackRankMate
        false, // hasSmotheredMate
        false, // hasSacrifice
        false, // hasZugzwang
        false, // hasDoubleCheck

        false, // hasOverloadedPiece
        Instant.now(),
        "pgn");
  }
}
