package com.muchq.games.one_d4.service;

import com.muchq.games.one_d4.api.dto.AnalyzeResponse;
import com.muchq.games.one_d4.api.dto.AnalyzedOccurrence;
import com.muchq.games.one_d4.engine.FeatureExtractor;
import com.muchq.games.one_d4.engine.model.GameFeatures;
import com.muchq.games.one_d4.engine.model.Motif;
import java.util.ArrayList;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Locale;
import java.util.Map;
import java.util.concurrent.ExecutionException;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Future;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.TimeoutException;

/**
 * Motif analysis of a single PGN, with nothing written to the corpus.
 *
 * <p>This lives here rather than in the MCP server because it has to agree with indexing. The
 * detectors produce a primitive {@link Motif#ATTACK} that no caller ever sees; fork, discovered
 * attack, discovered check, checkmate and double check are all derived from it, and the indexed
 * path derives them in SQL ({@code GameFeatureDao}). A second implementation in another process is
 * a second set of rules that can disagree with the first — and the disagreement would show up as
 * "analyze says fork, the query that should find it does not", which reads like a query bug.
 *
 * <p>Bounded on both axes an untrusted PGN can grow: {@link #MAX_PGN_BYTES} on the input, and a
 * wall-clock ceiling on the extraction, which runs on a shared pool so one pathological game cannot
 * hold a request thread.
 */
public class PositionAnalyzer {

  /**
   * Generous next to a real game — a long tournament PGN with full annotations runs a few tens of
   * KB — and small enough that replaying the result stays bounded. Analysis cost tracks move count,
   * and move count cannot exceed what fits here.
   */
  public static final int MAX_PGN_BYTES = 256 * 1024;

  private final FeatureExtractor featureExtractor;
  private final ExecutorService analysisPool;
  private final long timeoutMillis;

  public PositionAnalyzer(
      FeatureExtractor featureExtractor, ExecutorService analysisPool, long timeoutMillis) {
    this.featureExtractor = featureExtractor;
    this.analysisPool = analysisPool;
    this.timeoutMillis = timeoutMillis;
  }

  /**
   * @throws IllegalArgumentException if the PGN is missing, blank, too large, or unparseable
   * @throws AnalysisTimeoutException if extraction outruns the configured ceiling
   */
  public AnalyzeResponse analyze(String pgn) {
    if (pgn == null || pgn.isBlank()) {
      throw new IllegalArgumentException("pgn is required");
    }
    // Bytes, not chars: the limit exists to bound what was read off the wire, and a PGN with
    // multi-byte annotation text would slip past a length check.
    int bytes = pgn.getBytes(java.nio.charset.StandardCharsets.UTF_8).length;
    if (bytes > MAX_PGN_BYTES) {
      throw new IllegalArgumentException(
          "pgn is too large: " + bytes + " bytes (max " + MAX_PGN_BYTES + ")");
    }

    GameFeatures features = extractWithinTimeout(pgn);

    Map<Motif, List<GameFeatures.MotifOccurrence>> occurrences =
        new LinkedHashMap<>(features.occurrences());
    deriveAttackMotifs(occurrences);
    occurrences.remove(Motif.ATTACK); // internal primitive, not a caller-facing motif

    Map<String, List<AnalyzedOccurrence>> named = new LinkedHashMap<>();
    for (Map.Entry<Motif, List<GameFeatures.MotifOccurrence>> entry : occurrences.entrySet()) {
      if (!entry.getValue().isEmpty()) {
        named.put(entry.getKey().name().toLowerCase(Locale.ROOT), toRows(entry.getValue()));
      }
    }
    return new AnalyzeResponse(features.numMoves(), List.copyOf(named.keySet()), named);
  }

  private GameFeatures extractWithinTimeout(String pgn) {
    Future<GameFeatures> future = analysisPool.submit(() -> featureExtractor.extract(pgn));
    try {
      return future.get(timeoutMillis, TimeUnit.MILLISECONDS);
    } catch (TimeoutException e) {
      future.cancel(true);
      throw new AnalysisTimeoutException(
          "analysis exceeded " + timeoutMillis + "ms and was abandoned");
    } catch (ExecutionException e) {
      Throwable cause = e.getCause();
      // The extractor rejects unparseable PGN this way; surface it as the caller's problem
      // rather than a 500, since it is.
      if (cause instanceof IllegalArgumentException illegal) {
        throw illegal;
      }
      if (cause instanceof RuntimeException runtime) {
        throw runtime;
      }
      throw new IllegalStateException("analysis failed", cause);
    } catch (InterruptedException e) {
      Thread.currentThread().interrupt();
      throw new IllegalStateException("analysis interrupted", e);
    }
  }

  /**
   * Extraction outran its ceiling. Distinct from bad input, and answered as a 504 rather than 400.
   */
  public static class AnalysisTimeoutException extends RuntimeException {
    public AnalysisTimeoutException(String message) {
      super(message);
    }
  }

  private static List<AnalyzedOccurrence> toRows(List<GameFeatures.MotifOccurrence> occurrences) {
    return occurrences.stream()
        .map(
            o ->
                new AnalyzedOccurrence(
                    o.ply(),
                    o.moveNumber(),
                    o.side(),
                    o.description(),
                    o.movedPiece(),
                    o.attacker(),
                    o.target(),
                    o.isDiscovered(),
                    o.isMate(),
                    o.pinType()))
        .toList();
  }

  /**
   * Derives the ATTACK-based motifs the same way the indexer's SQL and response layers do, so
   * ad-hoc analysis matches what an indexed query would find for the same game.
   */
  private static void deriveAttackMotifs(
      Map<Motif, List<GameFeatures.MotifOccurrence>> occurrences) {
    List<GameFeatures.MotifOccurrence> attacks = occurrences.getOrDefault(Motif.ATTACK, List.of());
    if (attacks.isEmpty()) {
      return;
    }

    List<GameFeatures.MotifOccurrence> forks = new ArrayList<>();
    Map<String, List<GameFeatures.MotifOccurrence>> byPlyAttacker = new LinkedHashMap<>();
    for (GameFeatures.MotifOccurrence occ : attacks) {
      if (occ.attacker() != null && !occ.isDiscovered()) {
        byPlyAttacker
            .computeIfAbsent(occ.ply() + "|" + occ.attacker(), k -> new ArrayList<>())
            .add(occ);
      }
    }
    for (List<GameFeatures.MotifOccurrence> group : byPlyAttacker.values()) {
      if (group.size() >= 2) {
        forks.addAll(group);
      }
    }

    List<GameFeatures.MotifOccurrence> discoveredAttacks =
        attacks.stream().filter(GameFeatures.MotifOccurrence::isDiscovered).toList();
    List<GameFeatures.MotifOccurrence> checkmates =
        attacks.stream().filter(GameFeatures.MotifOccurrence::isMate).toList();
    List<GameFeatures.MotifOccurrence> discoveredChecks =
        attacks.stream().filter(o -> o.isDiscovered() && isKingTarget(o.target())).toList();

    List<GameFeatures.MotifOccurrence> doubleChecks = new ArrayList<>();
    Map<Integer, List<GameFeatures.MotifOccurrence>> kingAttacksByPly = new LinkedHashMap<>();
    for (GameFeatures.MotifOccurrence occ : attacks) {
      if (isKingTarget(occ.target())) {
        kingAttacksByPly.computeIfAbsent(occ.ply(), k -> new ArrayList<>()).add(occ);
      }
    }
    for (List<GameFeatures.MotifOccurrence> group : kingAttacksByPly.values()) {
      if (group.size() >= 2) {
        doubleChecks.add(group.get(0));
      }
    }

    putIfNonEmpty(occurrences, Motif.FORK, forks);
    putIfNonEmpty(occurrences, Motif.DISCOVERED_ATTACK, discoveredAttacks);
    putIfNonEmpty(occurrences, Motif.CHECKMATE, checkmates);
    putIfNonEmpty(occurrences, Motif.DISCOVERED_CHECK, discoveredChecks);
    putIfNonEmpty(occurrences, Motif.DOUBLE_CHECK, doubleChecks);
  }

  private static void putIfNonEmpty(
      Map<Motif, List<GameFeatures.MotifOccurrence>> occurrences,
      Motif motif,
      List<GameFeatures.MotifOccurrence> occs) {
    if (!occs.isEmpty() && !occurrences.containsKey(motif)) {
      occurrences.put(motif, occs);
    }
  }

  private static boolean isKingTarget(String target) {
    return target != null && (target.startsWith("K") || target.startsWith("k"));
  }
}
