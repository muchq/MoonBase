package com.muchq.games.one_d4.db;

import static org.assertj.core.api.Assertions.assertThat;
import static org.assertj.core.api.Assertions.assertThatThrownBy;

import com.muchq.games.one_d4.api.dto.GameFeature;
import com.muchq.games.one_d4.engine.model.GameFeatures;
import com.muchq.games.one_d4.engine.model.Motif;
import java.time.Duration;
import java.time.Instant;
import java.time.LocalDateTime;
import java.time.ZoneOffset;
import java.util.List;
import java.util.Map;
import java.util.UUID;
import java.util.concurrent.CountDownLatch;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;
import java.util.concurrent.Future;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.atomic.AtomicBoolean;
import java.util.concurrent.atomic.AtomicInteger;
import javax.sql.DataSource;
import org.junit.jupiter.api.AfterEach;
import org.junit.jupiter.api.BeforeEach;
import org.junit.jupiter.api.Test;
import org.junit.jupiter.api.Timeout;

/**
 * The write path's own safety, independent of who is allowed to run it.
 *
 * <p>#1249 named this as the harm behind the whole dedupe mechanism. A flush inserts features,
 * deletes the occurrences for those games, then re-inserts them. Run as three separate
 * transactions, two writers over the same game can interleave as delete, delete, insert, insert,
 * and {@code motif_occurrences} has no uniqueness beyond a random UUID primary key, so nothing
 * rejects the second copy: every motif for that game then reads double.
 *
 * <p>Until #1278 that was prevented only by making the overlap unreachable — one live request per
 * range, and a heartbeat so a running request is not retired underneath its worker. Those are good
 * defences and they are why this has not bitten, but they are defences in the control plane against
 * a defect in the data plane, and scale-out puts more processes in a position to test them. Two
 * live requests can also reach one game entirely legitimately: a game has two players, so indexing
 * each of them covers it, and dedupe cannot rule that out because the two requests differ.
 *
 * <p>Three properties are pinned here, in the order they build on each other: the hazard is real
 * when the delete and the insert are separate transactions; it is gone when they are one; and a
 * flush from a worker that no longer owns the request writes nothing at all.
 */
public class ConcurrentFlushTest {

  private static final String GAME_URL = "https://chess.com/game/contended";

  /** A game with no {@code game_features} row. Its occurrences violate the foreign key. */
  private static final String UNBACKED_URL = "https://chess.com/game/absent";

  private static final String OWNER_A = "host-a/1/aaaa";
  private static final String OWNER_B = "host-b/2/bbbb";
  private static final Instant NOW = Instant.parse("2026-07-01T12:00:00Z");

  /**
   * Enough flushes for a concurrent reader to land inside one, if landing inside one is possible.
   */
  private static final int ROUNDS = 200;

  private TestDb testDb;
  private DataSource dataSource;
  private GameFeatureDao store;
  private UUID requestId;
  private ExecutorService pool;

  @BeforeEach
  public void setUp() {
    testDb = TestDb.create("concurrent_flush");
    dataSource = testDb.dataSource();
    store = new GameFeatureDao(testDb.jdbi(), true);
    requestId = insertRequest(OWNER_A, NOW.plus(Duration.ofMinutes(5)));
    pool = Executors.newFixedThreadPool(2);
  }

  @AfterEach
  public void tearDown() {
    pool.shutdownNow();
  }

  /**
   * The hazard, stated as a fact rather than an argument. Driven through the separate public
   * primitives with the interleaving forced by a latch, doubling is not a race that might happen
   * but the guaranteed outcome of an ordering the API permits.
   *
   * <p>Those primitives are still public and still correct for their remaining caller — {@code
   * AdminController}'s reanalysis path pairs them, single-threaded — so the constraint on their use
   * needs somewhere to live. If a later change routes concurrent writers back through them, this is
   * the explanation waiting.
   */
  @Test
  @Timeout(30)
  public void theSeparateDeleteAndInsertPrimitivesAreNotSafeToPairUnderConcurrency()
      throws Exception {
    CountDownLatch bothDeleted = new CountDownLatch(2);
    CountDownLatch go = new CountDownLatch(1);

    Runnable flush =
        () -> {
          store.insertBatch(List.of(gameFeature()));
          store.deleteOccurrencesByGameUrls(List.of(GAME_URL));
          bothDeleted.countDown();
          try {
            // Hold here until the other writer has also deleted. This is the interleaving the
            // separate transactions allow; the latch only makes it deterministic.
            go.await(15, TimeUnit.SECONDS);
          } catch (InterruptedException e) {
            Thread.currentThread().interrupt();
          }
          store.insertOccurrencesBatch(Map.of(GAME_URL, occurrences()));
        };

    pool.submit(flush);
    pool.submit(flush);

    assertThat(bothDeleted.await(15, TimeUnit.SECONDS)).isTrue();
    go.countDown();
    pool.shutdown();
    assertThat(pool.awaitTermination(20, TimeUnit.SECONDS)).isTrue();

    assertThat(countOccurrences())
        .as("both inserts survived because neither delete could see the other's rows")
        .isEqualTo(4);
  }

  /**
   * The property that removes it, observed from outside: while a flush is in progress, nobody can
   * see the game with its occurrences deleted. The gap the interleaving needs is the same gap a
   * reader can see, so a reader that never sees one is evidence there is none to exploit.
   *
   * <p>This fails against the three-call sequence — the reader lands on a count of zero within a
   * few rounds — which is what makes it worth running rather than a loop that passes either way.
   */
  @Test
  @Timeout(60)
  public void aReaderNeverSeesAGameWithItsOccurrencesMissing() throws Exception {
    store.flushOwned(
        requestId, OWNER_A, NOW, List.of(gameFeature()), Map.of(GAME_URL, occurrences()));

    AtomicBoolean done = new AtomicBoolean(false);
    AtomicInteger lowest = new AtomicInteger(Integer.MAX_VALUE);

    Future<?> reader =
        pool.submit(
            () -> {
              while (!done.get()) {
                lowest.getAndUpdate(m -> Math.min(m, countOccurrences()));
              }
            });
    Future<?> writer =
        pool.submit(
            () -> {
              for (int i = 0; i < ROUNDS; i++) {
                store.flushOwned(
                    requestId,
                    OWNER_A,
                    NOW,
                    List.of(gameFeature()),
                    Map.of(GAME_URL, occurrences()));
              }
              done.set(true);
            });

    writer.get(45, TimeUnit.SECONDS);
    reader.get(45, TimeUnit.SECONDS);

    assertThat(lowest.get())
        .as("a reader observed a flush half applied, so another writer could have too")
        .isEqualTo(2);
  }

  /**
   * The same property from the other side, and deterministically: a flush that fails part way
   * through leaves the game exactly as it was. If the delete were its own transaction it would have
   * committed by the time the insert failed, and the game would be left with no occurrences at all
   * — the state a concurrent writer needs to duplicate into.
   *
   * <p>The failure is a real one rather than a stub: {@code motif_occurrences.game_url} is a
   * foreign key onto {@code game_features}, so occurrences for a game the batch does not also
   * insert cannot be written.
   */
  @Test
  public void aFlushThatFailsPartWayThroughLeavesThePreviousOccurrencesIntact() {
    store.flushOwned(
        requestId, OWNER_A, NOW, List.of(gameFeature()), Map.of(GAME_URL, occurrences()));
    assertThat(countOccurrences()).isEqualTo(2);

    assertThatThrownBy(
            () ->
                store.flushOwned(
                    requestId,
                    OWNER_A,
                    NOW,
                    List.of(gameFeature()),
                    Map.of(GAME_URL, occurrences(), UNBACKED_URL, occurrences())))
        .isInstanceOf(RuntimeException.class);

    assertThat(countOccurrences())
        .as("the delete outlived the insert that was supposed to replace it")
        .isEqualTo(2);
  }

  /** The fence: a worker whose lease has been taken writes nothing at all. */
  @Test
  public void aFlushIsRefusedOnceTheLeaseHasMovedToAnotherOwner() {
    setOwner(OWNER_B, NOW.plus(Duration.ofMinutes(5)));

    boolean wrote =
        store.flushOwned(
            requestId, OWNER_A, NOW, List.of(gameFeature()), Map.of(GAME_URL, occurrences()));

    assertThat(wrote).isFalse();
    assertThat(countOccurrences()).isZero();
    assertThat(countFeatures()).isZero();
  }

  /**
   * An expired lease is refused too, even though {@code owner_id} still names this worker. Past
   * expiry the system has already licensed a takeover, so writing would be racing a claim this
   * worker cannot see. Recovering from a lapse is the heartbeat's job, not the flush's.
   */
  @Test
  public void aFlushIsRefusedOnceTheLeaseHasExpired() {
    setOwner(OWNER_A, NOW.minusSeconds(1));

    boolean wrote =
        store.flushOwned(
            requestId, OWNER_A, NOW, List.of(gameFeature()), Map.of(GAME_URL, occurrences()));

    assertThat(wrote).isFalse();
    assertThat(countOccurrences()).isZero();
  }

  /** And once the request is no longer live, whoever owned it may not keep writing to it. */
  @Test
  public void aFlushIsRefusedOnceTheRequestHasBeenRetired() {
    execute(
        "UPDATE indexing_requests SET status = 'FAILED', owner_id = NULL,"
            + " lease_expires_at = NULL WHERE id = '"
            + requestId
            + "'");

    boolean wrote =
        store.flushOwned(
            requestId, OWNER_A, NOW, List.of(gameFeature()), Map.of(GAME_URL, occurrences()));

    assertThat(wrote).isFalse();
    assertThat(countOccurrences()).isZero();
  }

  /**
   * The status guard on its own, with {@code owner_id} deliberately left in place. Today every path
   * that ends a request also clears the owner, so this is unreachable — but it is unreachable
   * because of how the callers happen to be sequenced, which is exactly the kind of guarantee that
   * evaporates when someone adds a path. A finished request is not writable by anyone.
   */
  @Test
  public void aFlushIsRefusedOnATerminalRequestEvenIfItStillNamesTheWorker() {
    execute("UPDATE indexing_requests SET status = 'COMPLETED' WHERE id = '" + requestId + "'");

    boolean wrote =
        store.flushOwned(
            requestId, OWNER_A, NOW, List.of(gameFeature()), Map.of(GAME_URL, occurrences()));

    assertThat(wrote).isFalse();
    assertThat(countOccurrences()).isZero();
  }

  /** The happy path, so the refusals above are not passing for want of a working write. */
  @Test
  public void aFlushUnderALiveLeaseWritesBothGamesAndOccurrences() {
    boolean wrote =
        store.flushOwned(
            requestId, OWNER_A, NOW, List.of(gameFeature()), Map.of(GAME_URL, occurrences()));

    assertThat(wrote).isTrue();
    assertThat(countFeatures()).isEqualTo(1);
    assertThat(countOccurrences()).isEqualTo(2);
  }

  // --- helpers ---------------------------------------------------------------------------------

  private Map<Motif, List<GameFeatures.MotifOccurrence>> occurrences() {
    return Map.of(
        Motif.CHECK,
        List.of(
            new GameFeatures.MotifOccurrence(
                5, 3, "white", "check", null, null, null, false, false, null),
            new GameFeatures.MotifOccurrence(
                9, 5, "black", "check", null, null, null, false, false, null)));
  }

  private GameFeature gameFeature() {
    return new GameFeature(
        null,
        requestId,
        GAME_URL,
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
        Instant.parse("2024-01-15T00:00:00Z"),
        20,
        Instant.parse("2024-01-16T00:00:00Z"),
        "pgn");
  }

  private UUID insertRequest(String ownerId, Instant leaseExpiresAt) {
    UUID id = UUID.randomUUID();
    try (var conn = dataSource.getConnection();
        var ps =
            conn.prepareStatement(
                "INSERT INTO indexing_requests (id, player, platform, start_month, end_month,"
                    + " status, owner_id, lease_expires_at) VALUES (?, 'p', 'CHESS_COM', '2024-01',"
                    + " '2024-01', 'PROCESSING', ?, ?)")) {
      ps.setObject(1, id);
      ps.setString(2, ownerId);
      ps.setObject(3, utcWallClock(leaseExpiresAt));
      ps.executeUpdate();
    } catch (Exception e) {
      throw new RuntimeException(e);
    }
    return id;
  }

  private void setOwner(String ownerId, Instant leaseExpiresAt) {
    try (var conn = dataSource.getConnection();
        var ps =
            conn.prepareStatement(
                "UPDATE indexing_requests SET owner_id = ?, lease_expires_at = ? WHERE id = ?")) {
      ps.setString(1, ownerId);
      ps.setObject(2, utcWallClock(leaseExpiresAt));
      ps.setObject(3, requestId);
      ps.executeUpdate();
    } catch (Exception e) {
      throw new RuntimeException(e);
    }
  }

  /**
   * The lease column is TIMESTAMP WITHOUT TIME ZONE and the convention across these tables is that
   * it holds a UTC wall clock, so a fixture writing it directly has to say so rather than letting
   * the JVM's default zone decide.
   */
  private static LocalDateTime utcWallClock(Instant instant) {
    return instant.atOffset(ZoneOffset.UTC).toLocalDateTime();
  }

  private void execute(String sql) {
    try (var conn = dataSource.getConnection();
        var st = conn.createStatement()) {
      st.executeUpdate(sql);
    } catch (Exception e) {
      throw new RuntimeException(e);
    }
  }

  private int countOccurrences() {
    return count("SELECT COUNT(*) FROM motif_occurrences WHERE game_url = ?");
  }

  private int countFeatures() {
    return count("SELECT COUNT(*) FROM game_features WHERE game_url = ?");
  }

  private int count(String sql) {
    try (var conn = dataSource.getConnection();
        var ps = conn.prepareStatement(sql)) {
      ps.setString(1, GAME_URL);
      try (var rs = ps.executeQuery()) {
        rs.next();
        return rs.getInt(1);
      }
    } catch (Exception e) {
      throw new RuntimeException(e);
    }
  }
}
