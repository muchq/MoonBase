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

  /**
   * How long the rival in {@code aTakeoverThatCommitsDuringAFlushStillStopsIt} holds its row lock.
   * Long enough that the flush is certainly inside its ownership probe, short enough to stay under
   * H2's one-second default lock timeout so the blocked probe waits rather than erroring.
   */
  private static final long RIVAL_HOLD_MILLIS = 600;

  /**
   * How long a writer must fail to finish for us to call it blocked. Generously above what an
   * unblocked flush of one game takes, so the assertion cannot fire on a slow machine.
   */
  private static final long BLOCKED_WINDOW_MILLIS = 1500;

  /** Rounds of reanalysis against a flush. A race, not a proof — see the test that says so. */
  private static final int CONTENTION_ROUNDS = 60;

  private TestDb testDb;
  private DataSource dataSource;
  private GameFeatureDao store;
  private UUID requestId;
  private ExecutorService pool;

  @BeforeEach
  public void setUp() {
    testDb = TestDb.create("concurrent_flush");
    dataSource = testDb.dataSource();
    store = new GameFeatureDao(testDb.jdbi(), new H2SqlDialect());
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
   * <p>Those primitives are still public, and "single-threaded within its own loop" is not a
   * defence: {@code AdminController}'s reanalysis used to pair them, and the writer it races is an
   * indexing worker on another thread, not a second admin call. It now goes through {@code
   * replaceOccurrences}. This test is what the constraint on their use rests on — if a later change
   * routes any concurrent writer back through the pair, the explanation is waiting here.
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

  /**
   * The mechanism that makes reanalysis and an indexing flush safe against each other, asserted
   * directly rather than raced for.
   *
   * <p>Both rewrite a game's occurrences, and both being a transaction is not what saves them:
   * under READ COMMITTED a {@code DELETE} blocked on another transaction's uncommitted delete
   * re-checks the rows it blocked on but not the rows that transaction <em>inserted</em>, so they
   * still interleave as delete, delete, insert, insert. What saves them is contending for something
   * shared first, and the only thing they share is the game's {@code game_features} row.
   *
   * <p>Racing two writers cannot prove that — whichever finishes first, the count is right, so the
   * test passes with or without the lock. Holding the row from outside can: if a writer takes it,
   * it blocks; if it does not, it sails through. Delete either {@code lockGames} call and the
   * corresponding case fails immediately.
   */
  @Test
  @Timeout(30)
  public void bothOccurrenceWritersTakeTheGamesFeatureRowBeforeRewritingIt() throws Exception {
    store.flushOwned(
        requestId, OWNER_A, NOW, List.of(gameFeature()), Map.of(GAME_URL, occurrences()));

    assertBlocksWhileTheGameRowIsHeld(
        "reanalysis",
        () -> store.replaceOccurrences(List.of(GAME_URL), Map.of(GAME_URL, occurrences())));
    assertBlocksWhileTheGameRowIsHeld(
        "an indexing flush",
        () ->
            store.flushOwned(
                requestId, OWNER_A, NOW, List.of(gameFeature()), Map.of(GAME_URL, occurrences())));
    // The shape where the explicit lock is the only lock. A flush that writes features takes the
    // row implicitly — its upsert conflicts on the UNIQUE game_url — so deleting lockGames from
    // that path changes nothing observable and the case above passes either way. Occurrences
    // without features is reachable through the interface and rewrites the same rows with nothing
    // to conflict on, which is precisely what the explicit lock is for.
    assertBlocksWhileTheGameRowIsHeld(
        "a flush of occurrences alone",
        () ->
            store.flushOwned(requestId, OWNER_A, NOW, List.of(), Map.of(GAME_URL, occurrences())));
  }

  /**
   * Runs {@code writer} while another connection holds the game's {@code game_features} row, and
   * asserts it makes no progress until that is released.
   *
   * <p>The negative half is the load-bearing one and it is sound in the direction that matters: a
   * writer that takes the row lock <em>cannot</em> finish inside the window, so completing is proof
   * it never asked for it. The window is generous, so the converse — a writer slow for unrelated
   * reasons — cannot fail it.
   */
  private void assertBlocksWhileTheGameRowIsHeld(String what, Runnable writer) throws Exception {
    CountDownLatch rowHeld = new CountDownLatch(1);
    CountDownLatch releaseRow = new CountDownLatch(1);

    Future<?> holder =
        pool.submit(
            () -> {
              try (var conn = dataSource.getConnection()) {
                conn.setAutoCommit(false);
                try (var ps =
                    conn.prepareStatement(
                        "SELECT game_url FROM game_features WHERE game_url = ? FOR UPDATE")) {
                  ps.setString(1, GAME_URL);
                  ps.executeQuery().close();
                }
                rowHeld.countDown();
                releaseRow.await(20, TimeUnit.SECONDS);
                conn.commit();
              } catch (Exception e) {
                throw new RuntimeException(e);
              }
            });

    assertThat(rowHeld.await(15, TimeUnit.SECONDS)).isTrue();
    Future<?> blocked = pool.submit(writer);
    try {
      blocked.get(BLOCKED_WINDOW_MILLIS, TimeUnit.MILLISECONDS);
      throw new AssertionError(
          what + " rewrote the game's occurrences without taking its game_features row first");
    } catch (java.util.concurrent.TimeoutException expected) {
      // Blocked, which is the point.
    }

    releaseRow.countDown();
    blocked.get(20, TimeUnit.SECONDS);
    holder.get(20, TimeUnit.SECONDS);
  }

  /**
   * Both writers, run against each other repeatedly with a barrier per round.
   *
   * <p>This is a race and says so: it cannot force the delete/delete/insert/insert order, because
   * neither {@code flushOwned} nor {@code replaceOccurrences} can be paused mid-transaction from
   * outside. What it adds over the deterministic test above is coverage of orderings nobody thought
   * to write down. Treat a failure as real and a pass as weak evidence — the lock itself is pinned
   * by {@code bothOccurrenceWritersTakeTheGamesFeatureRowBeforeRewritingIt}, and the Postgres
   * behaviour that makes it necessary by {@code PostgresConcurrentWriteTest}.
   */
  @Test
  @Timeout(60)
  public void reanalysisAndAWorkerFlushOverOneGameDoNotDuplicateOccurrences() throws Exception {
    store.flushOwned(
        requestId, OWNER_A, NOW, List.of(gameFeature()), Map.of(GAME_URL, occurrences()));
    assertThat(countOccurrences()).isEqualTo(2);

    java.util.concurrent.CyclicBarrier round = new java.util.concurrent.CyclicBarrier(2);
    Runnable sync =
        () -> {
          try {
            round.await(20, TimeUnit.SECONDS);
          } catch (Exception e) {
            throw new RuntimeException(e);
          }
        };

    Future<?> reanalysis =
        pool.submit(
            () -> {
              for (int i = 0; i < CONTENTION_ROUNDS; i++) {
                sync.run();
                store.replaceOccurrences(List.of(GAME_URL), Map.of(GAME_URL, occurrences()));
              }
            });
    Future<?> flush =
        pool.submit(
            () -> {
              for (int i = 0; i < CONTENTION_ROUNDS; i++) {
                sync.run();
                store.flushOwned(
                    requestId,
                    OWNER_A,
                    NOW,
                    List.of(gameFeature()),
                    Map.of(GAME_URL, occurrences()));
              }
            });

    reanalysis.get(45, TimeUnit.SECONDS);
    flush.get(45, TimeUnit.SECONDS);

    assertThat(countOccurrences())
        .as("reanalysis and an indexing flush left duplicated motif rows for this game")
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
   *
   * <p>Asserted <em>at</em> the expiry instant, not a second past it. {@code claim} hands the row
   * over at {@code lease_expires_at <= now} while this predicate authorizes writing at {@code >
   * now}; they are complementary, and the only instant where that could break is the one they
   * share. Relaxing this to {@code >=} puts the replacement and the old owner on the same games for
   * that instant, and a test a second either side of the boundary would not notice.
   */
  @Test
  public void aFlushIsRefusedAtTheExactInstantTheLeaseExpires() {
    setOwner(OWNER_A, NOW);

    boolean wrote =
        store.flushOwned(
            requestId, OWNER_A, NOW, List.of(gameFeature()), Map.of(GAME_URL, occurrences()));

    assertThat(wrote).isFalse();
    assertThat(countOccurrences()).isZero();
  }

  /**
   * A takeover that commits <em>during</em> a flush, rather than before one.
   *
   * <p>This is the case a check-then-write cannot survive without a row lock. Nothing sets an
   * isolation level, so both engines run READ COMMITTED and a plain {@code SELECT} inside the
   * transaction is a snapshot, not a claim: the rival's {@code claim} commits in the gap and the
   * flush goes on to write rows for a request it no longer owns. The window is the whole flush.
   *
   * <p>Driven from the other side, because a test cannot pause the DAO mid-transaction: the rival
   * takes the lease first and holds its transaction open, then the flush runs. With {@code FOR
   * UPDATE} on the probe the flush blocks on the rival's uncommitted row and, once released, reads
   * the new owner and refuses. Without it the probe reads the pre-takeover snapshot and writes.
   */
  @Test
  @Timeout(30)
  public void aTakeoverThatCommitsDuringAFlushStillStopsIt() throws Exception {
    CountDownLatch rivalHoldsRow = new CountDownLatch(1);
    // Never counted down. The rival holds its row lock for the whole timeout, which has to outlast
    // the flush reaching its ownership probe — otherwise the rival commits first, the probe reads
    // the new owner from a committed snapshot, and the test passes whether or not it took a lock.
    CountDownLatch neverReleased = new CountDownLatch(1);

    Future<?> rival =
        pool.submit(
            () -> {
              try (var conn = dataSource.getConnection()) {
                conn.setAutoCommit(false);
                try (var ps =
                    conn.prepareStatement(
                        "UPDATE indexing_requests SET owner_id = ?, lease_expires_at = ?"
                            + " WHERE id = ?")) {
                  ps.setString(1, OWNER_B);
                  ps.setObject(2, utcWallClock(NOW.plus(Duration.ofMinutes(5))));
                  ps.setObject(3, requestId);
                  ps.executeUpdate();
                }
                rivalHoldsRow.countDown();
                neverReleased.await(RIVAL_HOLD_MILLIS, TimeUnit.MILLISECONDS);
                conn.commit();
              } catch (Exception e) {
                throw new RuntimeException(e);
              }
            });

    assertThat(rivalHoldsRow.await(15, TimeUnit.SECONDS)).isTrue();
    Future<Boolean> flush =
        pool.submit(
            () ->
                store.flushOwned(
                    requestId,
                    OWNER_A,
                    NOW,
                    List.of(gameFeature()),
                    Map.of(GAME_URL, occurrences())));

    assertThat(flush.get(20, TimeUnit.SECONDS))
        .as("the flush committed against a request that had already changed hands")
        .isFalse();
    rival.get(20, TimeUnit.SECONDS);
    assertThat(countOccurrences()).isZero();
    assertThat(countFeatures()).isZero();
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
