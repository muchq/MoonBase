package com.muchq.games.one_d4.db;

import static org.assertj.core.api.Assertions.assertThat;

import java.time.Instant;
import javax.sql.DataSource;
import org.junit.Before;
import org.junit.Test;

public class IndexedPeriodDaoTest {

  private IndexedPeriodDao dao;

  @Before
  public void setUp() {
    String jdbcUrl = "jdbc:h2:mem:period_test;DB_CLOSE_DELAY=-1";
    DataSource dataSource = DataSourceFactory.create(jdbcUrl, "sa", "");
    Migration migration = new Migration(dataSource, true);
    migration.run();
    dao = new IndexedPeriodDao(dataSource, true);
  }

  @Test
  public void findCompletePeriod_returnsEmptyWhenNone() {
    assertThat(dao.findCompletePeriod("p", "CHESS_COM", "2024-01", false)).isEmpty();
  }

  @Test
  public void upsertAndFindCompletePeriod_returnsPeriodWhenComplete() {
    Instant fetchedAt = Instant.parse("2024-02-15T12:00:00Z");
    dao.upsertPeriod("player1", "CHESS_COM", "2024-01", fetchedAt, true, 42, false);
    var result = dao.findCompletePeriod("player1", "CHESS_COM", "2024-01", false);
    assertThat(result).isPresent();
    assertThat(result.get().player()).isEqualTo("player1");
    assertThat(result.get().platform()).isEqualTo("CHESS_COM");
    assertThat(result.get().month()).isEqualTo("2024-01");
    assertThat(result.get().gamesCount()).isEqualTo(42);
    assertThat(result.get().isComplete()).isTrue();
    assertThat(result.get().excludeBullet()).isFalse();
  }

  @Test
  public void findCompletePeriod_returnsEmptyWhenIncomplete() {
    Instant fetchedAt = Instant.parse("2024-01-15T12:00:00Z");
    dao.upsertPeriod("player2", "CHESS_COM", "2024-01", fetchedAt, false, 10, false);
    assertThat(dao.findCompletePeriod("player2", "CHESS_COM", "2024-01", false)).isEmpty();
  }

  @Test
  public void upsertOverwritesExistingPeriod() {
    dao.upsertPeriod("player3", "CHESS_COM", "2024-03", Instant.EPOCH, true, 1, false);
    dao.upsertPeriod(
        "player3", "CHESS_COM", "2024-03", Instant.parse("2024-04-01T00:00:00Z"), true, 99, false);
    var result = dao.findCompletePeriod("player3", "CHESS_COM", "2024-03", false);
    assertThat(result).isPresent();
    assertThat(result.get().gamesCount()).isEqualTo(99);
  }

  @Test
  public void findCompletePeriod_distinguishesByExcludeBullet() {
    Instant fetchedAt = Instant.parse("2024-02-01T00:00:00Z");
    dao.upsertPeriod("player4", "CHESS_COM", "2024-01", fetchedAt, true, 10, false);
    dao.upsertPeriod("player4", "CHESS_COM", "2024-01", fetchedAt, true, 7, true);

    var withBullet = dao.findCompletePeriod("player4", "CHESS_COM", "2024-01", false);
    assertThat(withBullet).isPresent();
    assertThat(withBullet.get().gamesCount()).isEqualTo(10);

    var withoutBullet = dao.findCompletePeriod("player4", "CHESS_COM", "2024-01", true);
    assertThat(withoutBullet).isPresent();
    assertThat(withoutBullet.get().gamesCount()).isEqualTo(7);
  }
}
