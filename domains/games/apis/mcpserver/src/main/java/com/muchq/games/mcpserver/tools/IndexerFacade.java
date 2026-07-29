package com.muchq.games.mcpserver.tools;

import com.muchq.games.chessql.compiler.CompiledQuery;
import com.muchq.games.chessql.compiler.SqlCompiler;
import com.muchq.games.chessql.parser.ParsedQuery;
import com.muchq.games.chessql.parser.Parser;
import com.muchq.games.one_d4.api.dto.AggregateRow;
import com.muchq.games.one_d4.api.dto.GameFeature;
import com.muchq.games.one_d4.api.dto.GameFeatureRow;
import com.muchq.games.one_d4.api.dto.IndexResponse;
import com.muchq.games.one_d4.api.dto.OccurrenceRow;
import com.muchq.games.one_d4.db.GameFeatureStore;
import com.muchq.games.one_d4.db.IndexingRequestStore;
import com.muchq.games.one_d4.engine.FeatureExtractor;
import com.muchq.games.one_d4.engine.model.GameFeatures;
import com.muchq.games.one_d4.engine.model.Motif;
import com.muchq.games.one_d4.queue.IndexMessage;
import com.muchq.games.one_d4.queue.IndexQueue;
import com.muchq.games.one_d4.worker.IndexWorker;
import java.time.YearMonth;
import java.time.format.DateTimeParseException;
import java.time.temporal.ChronoUnit;
import java.util.ArrayList;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Locale;
import java.util.Map;
import java.util.Optional;
import java.util.UUID;

/**
 * Thin in-process wrapper over the one_d4 indexer components (see one_d4/docs/MCP_INTEGRATION.md,
 * Option A). MCP tools delegate here instead of reaching into indexer internals.
 */
public class IndexerFacade {

  static final int MAX_MONTH_SPAN = 12;

  private final IndexingRequestStore requestStore;
  private final GameFeatureStore gameFeatureStore;
  private final IndexQueue queue;
  private final IndexWorker worker;
  private final FeatureExtractor featureExtractor;
  private final SqlCompiler sqlCompiler;

  public IndexerFacade(
      IndexingRequestStore requestStore,
      GameFeatureStore gameFeatureStore,
      IndexQueue queue,
      IndexWorker worker,
      FeatureExtractor featureExtractor,
      SqlCompiler sqlCompiler) {
    this.requestStore = requestStore;
    this.gameFeatureStore = gameFeatureStore;
    this.queue = queue;
    this.worker = worker;
    this.featureExtractor = featureExtractor;
    this.sqlCompiler = sqlCompiler;
  }

  /**
   * Starts (or reuses) an indexing request. Single-month requests run synchronously so the caller
   * gets a final status in one round trip; multi-month requests are enqueued and can be polled via
   * {@link #status}.
   */
  public IndexResponse index(
      String player, String platform, String startMonth, String endMonth, boolean excludeBullet) {
    if (player == null || player.isBlank()) {
      throw new IllegalArgumentException("username is required");
    }
    String canonicalPlatform = canonicalPlatform(platform);
    YearMonth start = parseMonth(startMonth, "start_month");
    YearMonth end = parseMonth(endMonth, "end_month");
    if (start.isAfter(end)) {
      throw new IllegalArgumentException("start_month must not be after end_month");
    }
    long monthSpan = start.until(end, ChronoUnit.MONTHS) + 1;
    if (monthSpan > MAX_MONTH_SPAN) {
      throw new IllegalArgumentException(
          "Maximum range is " + MAX_MONTH_SPAN + " months, got " + monthSpan);
    }

    Optional<IndexingRequestStore.IndexingRequest> existing =
        requestStore.findExistingRequest(
            player, canonicalPlatform, startMonth, endMonth, excludeBullet);
    if (existing.isPresent()) {
      return toResponse(existing.get());
    }

    UUID id = requestStore.create(player, canonicalPlatform, startMonth, endMonth, excludeBullet);
    IndexMessage message =
        new IndexMessage(id, player, canonicalPlatform, startMonth, endMonth, excludeBullet);

    if (monthSpan <= 1) {
      // Small request: process inline (typically well under a minute) and return final status.
      worker.process(message);
      return status(id)
          .orElse(
              new IndexResponse(
                  id,
                  player,
                  canonicalPlatform,
                  startMonth,
                  endMonth,
                  "UNKNOWN",
                  0,
                  null,
                  excludeBullet));
    }

    queue.enqueue(message);
    return new IndexResponse(
        id, player, canonicalPlatform, startMonth, endMonth, "PENDING", 0, null, excludeBullet);
  }

  public Optional<IndexResponse> status(UUID requestId) {
    return requestStore.findById(requestId).map(IndexerFacade::toResponse);
  }

  /** Runs a ChessQL query over indexed games and returns rows with their motif occurrences. */
  public List<GameFeatureRow> query(String chessql, int limit) {
    ParsedQuery parsed = Parser.parse(chessql);
    CompiledQuery compiled = sqlCompiler.compile(parsed);
    List<GameFeature> rows = gameFeatureStore.query(compiled, limit, 0);

    List<String> gameUrls = rows.stream().map(GameFeature::gameUrl).toList();
    Map<String, Map<String, List<OccurrenceRow>>> occurrences =
        gameFeatureStore.queryOccurrences(gameUrls);

    return rows.stream()
        .map(
            row -> GameFeatureRow.fromStore(row, occurrences.getOrDefault(row.gameUrl(), Map.of())))
        .toList();
  }

  /** Counts indexed games matching a ChessQL filter, grouped by the given fields. */
  public List<AggregateRow> aggregate(String chessql, List<String> groupBy, int limit) {
    ParsedQuery parsed = Parser.parse(chessql);
    List<String> groupColumns = sqlCompiler.resolveGroupByColumns(groupBy);
    CompiledQuery compiled = sqlCompiler.compileAggregate(parsed, groupBy);
    return gameFeatureStore.aggregate(compiled, groupColumns, limit);
  }

  /** Detects motifs in a single PGN without indexing it. */
  public AnalysisResult analyze(String pgn) {
    if (pgn == null || pgn.isBlank()) {
      throw new IllegalArgumentException("pgn is required");
    }
    GameFeatures features = featureExtractor.extract(pgn);

    Map<Motif, List<GameFeatures.MotifOccurrence>> occurrences =
        new LinkedHashMap<>(features.occurrences());
    deriveAttackMotifs(occurrences);
    occurrences.remove(Motif.ATTACK); // internal primitive, not a caller-facing motif

    Map<String, List<GameFeatures.MotifOccurrence>> named = new LinkedHashMap<>();
    for (Map.Entry<Motif, List<GameFeatures.MotifOccurrence>> entry : occurrences.entrySet()) {
      if (!entry.getValue().isEmpty()) {
        named.put(entry.getKey().name().toLowerCase(Locale.ROOT), entry.getValue());
      }
    }
    return new AnalysisResult(features.numMoves(), List.copyOf(named.keySet()), named);
  }

  public record AnalysisResult(
      int numMoves,
      List<String> motifs,
      Map<String, List<GameFeatures.MotifOccurrence>> occurrences) {}

  /**
   * Derives the ATTACK-based motifs (fork, discovered attack/check, checkmate, double check) the
   * same way the indexer's SQL and response layers do, so ad-hoc analysis matches indexed-query
   * results.
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

  private static IndexResponse toResponse(IndexingRequestStore.IndexingRequest row) {
    return new IndexResponse(
        row.id(),
        row.player(),
        row.platform(),
        row.startMonth(),
        row.endMonth(),
        row.status(),
        row.gamesIndexed(),
        row.errorMessage(),
        row.excludeBullet());
  }

  private static String canonicalPlatform(String platform) {
    if (platform == null || platform.isBlank()) {
      throw new IllegalArgumentException("platform is required");
    }
    String normalized = platform.strip().toUpperCase(Locale.ROOT).replace('.', '_');
    if (!"CHESS_COM".equals(normalized)) {
      throw new IllegalArgumentException(
          "Unsupported platform: " + platform + ". Supported: chess.com");
    }
    return "CHESS_COM";
  }

  private static YearMonth parseMonth(String value, String fieldName) {
    if (value == null || value.isBlank()) {
      throw new IllegalArgumentException(fieldName + " is required");
    }
    try {
      return YearMonth.parse(value);
    } catch (DateTimeParseException e) {
      throw new IllegalArgumentException(fieldName + " must be in YYYY-MM format, got: " + value);
    }
  }
}
