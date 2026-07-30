package com.muchq.games.one_d4.db;

import static org.assertj.core.api.Assertions.assertThat;

import com.muchq.games.chessql.compiler.SqlCompiler;
import com.muchq.games.chessql.parser.Parser;
import com.muchq.games.one_d4.api.dto.GameFeature;
import java.time.Instant;
import java.util.List;
import java.util.TimeZone;
import java.util.UUID;
import org.junit.jupiter.api.BeforeEach;
import org.junit.jupiter.api.Test;

/**
 * ChessQL's {@code date} / {@code month} fields compile to played_at comparisons against UTC
 * day/month boundaries bound as {@link java.sql.Timestamp}. played_at is a TIMESTAMP column with no
 * zone on both H2 and Postgres, and {@code setTimestamp} without an explicit Calendar converts
 * through the JVM's default zone — so the whole feature is only correct if the write side and the
 * query side agree on that zone. This suite runs the end-to-end path under a deliberately extreme
 * non-UTC default zone (Pacific/Kiritimati, UTC+14) to prove the day boundaries stay UTC days
 * rather than sliding with the JVM.
 *
 * <p>The zone comes from the Bazel target's {@code env = {"TZ": ...}} rather than {@code
 * TimeZone.setDefault} in a {@code @BeforeEach}: H2 caches the default zone globally the first time
 * it converts a value, so a zone changed mid-JVM would not reach the driver and the test would pass
 * vacuously. {@code GameFeatureDaoTest} covers the same path under the container's default zone.
 */
public class PlayedAtTimeZoneTest {

  /** UTC+14 with no DST — the largest offset in the tz database, so any zone leak shows up. */
  private static final String EXPECTED_ZONE = "Pacific/Kiritimati";

  private TestDb testDb;
  private GameFeatureDao dao;
  private UUID requestId;

  @BeforeEach
  public void setUp() {
    testDb = TestDb.create("playedattz");
    dao = new GameFeatureDao(testDb.jdbi(), true);
    requestId = UUID.randomUUID();
    try (var conn = testDb.dataSource().getConnection();
        var stmt =
            conn.prepareStatement(
                "INSERT INTO indexing_requests (id, player, platform, start_month, end_month,"
                    + " status) VALUES (?, 'p', 'CHESS_COM', '2026-06', '2026-07', 'COMPLETED')")) {
      stmt.setObject(1, requestId);
      stmt.executeUpdate();
    } catch (Exception e) {
      throw new RuntimeException(e);
    }
  }

  /**
   * Guards the rest of the suite: if the target's TZ env stops reaching the JVM, every other
   * assertion here silently degrades into a duplicate of the UTC coverage.
   */
  @Test
  public void testRunsUnderTheIntendedNonUtcZone() {
    assertThat(TimeZone.getDefault().getID()).isEqualTo(EXPECTED_ZONE);
    assertThat(TimeZone.getDefault().getRawOffset()).isEqualTo(14 * 60 * 60 * 1000);
  }

  /**
   * Under UTC+14 these three instants have local calendar days one ahead of their UTC days, so a
   * date rewrite that leaked the JVM zone would put them in the wrong bucket.
   */
  @Test
  public void dateAndMonthBoundariesStayUtcUnderNonUtcJvmZone() {
    dao.insertBatch(
        List.of(
            // 2026-07-01T00:00 local in Kiritimati, but still June 30 in UTC
            gameAt("juneEnd", Instant.parse("2026-06-30T23:59:59.999Z")),
            // 2026-07-01T14:00 local — the first instant of UTC July
            gameAt("julyStart", Instant.parse("2026-07-01T00:00:00.000Z")),
            gameAt("julyMid", Instant.parse("2026-07-15T12:00:00.000Z")));

    assertThat(urlsMatching("month = \"2026-06\"")).containsExactly("juneEnd");
    assertThat(urlsMatching("month = \"2026-07\"")).containsExactly("julyMid", "julyStart");
    assertThat(urlsMatching("date = \"2026-06-30\"")).containsExactly("juneEnd");
    assertThat(urlsMatching("date = \"2026-07-01\"")).containsExactly("julyStart");
    assertThat(urlsMatching("date <= \"2026-06-30\"")).containsExactly("juneEnd");
    assertThat(urlsMatching("date > \"2026-06-30\"")).containsExactly("julyMid", "julyStart");
    assertThat(urlsMatching("date < \"2026-07-01\"")).containsExactly("juneEnd");
    assertThat(urlsMatching("date >= \"2026-07-01\"")).containsExactly("julyMid", "julyStart");
    assertThat(urlsMatching("date != \"2026-07-01\"")).containsExactly("juneEnd", "julyMid");
  }

  /** played_at must survive the round trip as the same instant, not shifted by the zone offset. */
  @Test
  public void playedAtRoundTripsAsTheSameInstantUnderNonUtcJvmZone() {
    Instant played = Instant.parse("2026-07-01T00:00:00Z");
    dao.insertBatch(List.of(gameAt("julyStart", played)));

    List<GameFeature> rows =
        dao.query(new SqlCompiler().compile(Parser.parse("month = \"2026-07\"")), 10, 0);
    assertThat(rows).hasSize(1);
    assertThat(rows.get(0).playedAt()).isEqualTo(played);
  }

  /** The aggregate and totals paths carry the same timestamp binds, so they must agree too. */
  @Test
  public void aggregateAndTotalsAgreeOnDateScopeUnderNonUtcJvmZone() {
    dao.insertBatch(
        List.of(
            gameAt("juneEnd", Instant.parse("2026-06-30T23:59:59.999Z")),
            gameAt("julyStart", Instant.parse("2026-07-01T00:00:00.000Z")),
            gameAt("julyMid", Instant.parse("2026-07-15T12:00:00.000Z"))));

    SqlCompiler compiler = new SqlCompiler();
    var parsed = Parser.parse("month = \"2026-07\"");
    var groups =
        dao.aggregate(
            compiler.compileAggregate(parsed, List.of("time_class")), List.of("time_class"), 10);
    var totals = dao.aggregateTotals(compiler.compileAggregateTotals(parsed, List.of("time_class")));

    assertThat(groups).hasSize(1);
    assertThat(groups.get(0).count()).isEqualTo(2);
    assertThat(totals.totalGames()).isEqualTo(2);
    assertThat(totals.totalGroups()).isEqualTo(1);
  }

  private List<String> urlsMatching(String chessql) {
    return dao.query(new SqlCompiler().compile(Parser.parse(chessql)), 50, 0).stream()
        .map(GameFeature::gameUrl)
        .sorted()
        .toList();
  }

  private GameFeature gameAt(String url, Instant playedAt) {
    return new GameFeature(
        null,
        requestId,
        url,
        "CHESS_COM",
        "white",
        "black",
        1500,
        1500,
        null,
        null,
        "blitz",
        "A00",
        null,
        null,
        "1-0",
        playedAt,
        30,
        Instant.now(),
        "1. e4 e5 *");
  }
}
