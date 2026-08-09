package com.muchq.games.one_d4.service;

import static org.assertj.core.api.Assertions.assertThat;
import static org.assertj.core.api.Assertions.assertThatThrownBy;

import com.muchq.games.one_d4.api.dto.AnalyzeResponse;
import com.muchq.games.one_d4.engine.FeatureExtractor;
import com.muchq.games.one_d4.engine.GameReplayer;
import com.muchq.games.one_d4.engine.PgnParser;
import com.muchq.games.one_d4.engine.model.GameFeatures;
import com.muchq.games.one_d4.engine.model.Motif;
import com.muchq.games.one_d4.motifs.Detectors;
import java.util.concurrent.CountDownLatch;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;
import java.util.concurrent.TimeUnit;
import org.junit.jupiter.api.AfterAll;
import org.junit.jupiter.api.BeforeAll;
import org.junit.jupiter.api.Test;

/**
 * The analysis behind POST /v1/analyze, and the bounds on it.
 *
 * <p>The bounds are the point of most of this file. Analysis is CPU on caller-supplied input with
 * nothing persisted, so the two things that can go wrong are an input too big to be worth replaying
 * and a game that takes longer than anyone is waiting — and neither fails visibly without a test,
 * because the happy path is identical right up until the service is wedged.
 */
public class PositionAnalyzerTest {

  /** Ends 54. Ra5#, with pins, forks and promotions along the way. */
  private static final String PGN =
      """
      [Event "Live Chess"]
      [Result "1-0"]

      1. e4 e5 2. f4 d6 3. Nf3 Nc6 4. Bb5 Bd7 5. Nc3 f6 6. f5 Be7 7. Nh4 h5 \
      8. Ng6 Rh6 9. Nd5 Nd4 10. Bxd7+ Qxd7 11. d3 Rh7 12. h4 c6 13. Ngxe7 Nxe7 \
      14. Nxe7 Kxe7 15. Be3 c5 16. g4 hxg4 17. Qxg4 Qa4 18. Bxd4 cxd4 19. Qg6 Rah8 \
      20. a3 Qxc2 21. O-O Rxh4 22. Qxg7+ Ke8 23. Qg6+ Kf8 24. Qxf6+ Ke8 25. Qe6+ Kd8 \
      26. Qxd6+ Kc8 27. Qe6+ Kb8 28. Qxe5+ Ka8 29. Rf2 Rh1+ 30. Kg2 R8h2+ 31. Qxh2 Rxh2+ \
      32. Kxh2 Qxf2+ 33. Kh1 Qxb2 34. Rg1 a6 35. f6 Qf2 36. e5 Qf3+ 37. Kh2 Qf4+ \
      38. Rg3 Qxe5 39. f7 Qh5+ 40. Kg2 Qxf7 41. Rf3 Qa2+ 42. Kg3 Qxa3 43. Kf4 Qf8+ \
      44. Ke4 Qe8+ 45. Kxd4 Qd7+ 46. Ke5 a5 47. d4 a4 48. d5 Qg7+ 49. Ke6 Qg4+ \
      50. Rf5 a3 51. d6 Kb8 52. d7 Qg7 53. d8=Q+ Ka7 54. Ra5# 1-0
      """;

  private static ExecutorService pool;

  @BeforeAll
  public static void startPool() {
    pool = Executors.newFixedThreadPool(2);
  }

  @AfterAll
  public static void stopPool() {
    pool.shutdownNow();
  }

  private static PositionAnalyzer analyzer(long timeoutMillis) {
    return new PositionAnalyzer(productionExtractor(), pool, timeoutMillis);
  }

  /**
   * Built from {@link Detectors#defaultDetectors()} — the same list IndexerModule hands the
   * indexer. Analysis that ran a different detector set would answer "does this game contain a
   * fork" differently from the query that is supposed to find that same game, and the mismatch
   * would read as a query bug rather than an analysis one.
   */
  private static FeatureExtractor productionExtractor() {
    return new FeatureExtractor(new PgnParser(), new GameReplayer(), Detectors.defaultDetectors());
  }

  @Test
  public void analyzeReportsMoveCountAndMotifs() {
    AnalyzeResponse response = analyzer(30_000).analyze(PGN);

    assertThat(response.numMoves()).isEqualTo(54);
    assertThat(response.motifs()).isNotEmpty();
    assertThat(response.motifs())
        .as("the game ends in mate, so checkmate has to be among them")
        .contains("checkmate");
  }

  /**
   * {@code motifs} is documented as exactly the key set of {@code occurrences}. A caller that
   * branches on the list and then indexes into the map would NPE on any drift, so the two are
   * pinned against each other rather than each against a literal.
   */
  @Test
  public void motifsListMatchesTheOccurrenceKeys() {
    AnalyzeResponse response = analyzer(30_000).analyze(PGN);

    assertThat(response.motifs())
        .containsExactlyInAnyOrderElementsOf(response.occurrences().keySet());
    assertThat(response.occurrences().values())
        .as("an empty occurrence list would be a motif claimed but not evidenced")
        .allSatisfy(list -> assertThat(list).isNotEmpty());
  }

  /**
   * ATTACK is the detectors' internal primitive: fork, discovered attack, discovered check,
   * checkmate and double check are all derived from it, and it is not a motif any caller should
   * see. Leaking it would put a name in the response that no ChessQL query can match.
   */
  @Test
  public void theAttackPrimitiveIsNotAdvertisedAsAMotif() {
    AnalyzeResponse response = analyzer(30_000).analyze(PGN);

    assertThat(response.motifs()).doesNotContain("attack");
    assertThat(response.occurrences()).doesNotContainKey("attack");
  }

  /**
   * A detector can report a motif key with nothing under it. Advertising that would claim a motif
   * the game does not contain — worse than a missing one, because a caller has no way to tell it
   * apart from a real detection without inspecting the empty list.
   *
   * <p>Forced rather than found: no real game in these fixtures produces an empty list, so the
   * filter is unreachable from a PGN and a mutation removing it survives everything else here.
   */
  @Test
  public void aMotifWithNoOccurrencesIsNotAdvertised() {
    FeatureExtractor emptyPin =
        new FeatureExtractor(new PgnParser(), new GameReplayer(), Detectors.defaultDetectors()) {
          @Override
          public GameFeatures extract(String pgn) {
            return new GameFeatures(
                java.util.EnumSet.of(Motif.PIN, Motif.CHECK),
                12,
                java.util.Map.of(
                    Motif.PIN,
                    java.util.List.of(),
                    Motif.CHECK,
                    java.util.List.of(
                        new GameFeatures.MotifOccurrence(
                            3, 2, "white", "check", null, "Q", "k", false, false, null))));
          }
        };

    AnalyzeResponse response = new PositionAnalyzer(emptyPin, pool, 30_000).analyze(PGN);

    assertThat(response.motifs()).containsExactly("check");
    assertThat(response.occurrences()).doesNotContainKey("pin");
  }

  @Test
  public void blankOrMissingPgnIsRejected() {
    PositionAnalyzer analyzer = analyzer(30_000);

    assertThatThrownBy(() -> analyzer.analyze(null))
        .isInstanceOf(IllegalArgumentException.class)
        .hasMessageContaining("pgn is required");
    assertThatThrownBy(() -> analyzer.analyze("   \n  "))
        .isInstanceOf(IllegalArgumentException.class)
        .hasMessageContaining("pgn is required");
  }

  /**
   * The size limit is what keeps analysis cost bounded — move count cannot exceed what fits in the
   * cap, and replay cost tracks move count. Rejected before extraction, so an oversized body never
   * reaches the parser.
   */
  @Test
  public void oversizedPgnIsRejectedBeforeAnalysis() {
    String huge = "1. e4 e5 ".repeat(PositionAnalyzer.MAX_PGN_BYTES / 4);

    assertThatThrownBy(() -> analyzer(30_000).analyze(huge))
        .isInstanceOf(IllegalArgumentException.class)
        .hasMessageContaining("too large");
  }

  /**
   * Measured in bytes rather than characters: the cap exists to bound what was read off the wire,
   * and a PGN whose annotations are multi-byte would slip past a {@code length()} check by up to a
   * factor of three.
   */
  @Test
  public void theSizeLimitCountsBytesNotCharacters() {
    // Each of these is 3 bytes in UTF-8 and one char, so this is under the cap by chars and over
    // it by bytes.
    String multiByte = "♞".repeat((PositionAnalyzer.MAX_PGN_BYTES / 3) + 1);
    assertThat(multiByte.length()).isLessThan(PositionAnalyzer.MAX_PGN_BYTES);

    assertThatThrownBy(() -> analyzer(30_000).analyze(multiByte))
        .isInstanceOf(IllegalArgumentException.class)
        .hasMessageContaining("too large");
  }

  /**
   * A game that outruns the ceiling has to end as a timeout the caller can retry, not as a request
   * thread parked forever. The extractor here blocks until released, which is the worst case the
   * ceiling exists for.
   */
  @Test
  public void extractionThatOutrunsTheCeilingTimesOut() throws Exception {
    CountDownLatch release = new CountDownLatch(1);
    FeatureExtractor blocking =
        new FeatureExtractor(new PgnParser(), new GameReplayer(), Detectors.defaultDetectors()) {
          @Override
          public GameFeatures extract(String pgn) {
            try {
              release.await(30, TimeUnit.SECONDS);
            } catch (InterruptedException e) {
              Thread.currentThread().interrupt();
            }
            return super.extract(pgn);
          }
        };

    try {
      assertThatThrownBy(() -> new PositionAnalyzer(blocking, pool, 100).analyze(PGN))
          .isInstanceOf(PositionAnalyzer.AnalysisTimeoutException.class)
          .hasMessageContaining("100ms");
    } finally {
      release.countDown();
    }
  }

  /**
   * A timeout must not poison the pool for the next caller. Two threads and a blocked task means a
   * second request still has somewhere to run — but only if the abandoned one was cancelled rather
   * than left holding its thread forever.
   */
  @Test
  public void thePoolStillServesRequestsAfterATimeout() throws Exception {
    CountDownLatch release = new CountDownLatch(1);
    FeatureExtractor blocking =
        new FeatureExtractor(new PgnParser(), new GameReplayer(), Detectors.defaultDetectors()) {
          @Override
          public GameFeatures extract(String pgn) {
            try {
              release.await(30, TimeUnit.SECONDS);
            } catch (InterruptedException e) {
              Thread.currentThread().interrupt();
            }
            return super.extract(pgn);
          }
        };

    try {
      assertThatThrownBy(() -> new PositionAnalyzer(blocking, pool, 100).analyze(PGN))
          .isInstanceOf(PositionAnalyzer.AnalysisTimeoutException.class);

      assertThat(analyzer(30_000).analyze(PGN).numMoves()).isEqualTo(54);
    } finally {
      release.countDown();
    }
  }
}
