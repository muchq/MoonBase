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
import com.muchq.games.one_d4.engine.FeatureExtractor;
import com.muchq.games.one_d4.engine.model.GameFeatures;
import com.muchq.games.one_d4.engine.model.Motif;
import com.muchq.games.one_d4.service.IndexRequestService;
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

  private final IndexRequestService indexRequestService;
  private final GameFeatureStore gameFeatureStore;
  private final FeatureExtractor featureExtractor;
  private final SqlCompiler sqlCompiler;

  public IndexerFacade(
      IndexRequestService indexRequestService,
      GameFeatureStore gameFeatureStore,
      FeatureExtractor featureExtractor,
      SqlCompiler sqlCompiler) {
    this.indexRequestService = indexRequestService;
    this.gameFeatureStore = gameFeatureStore;
    this.featureExtractor = featureExtractor;
    this.sqlCompiler = sqlCompiler;
  }

  /**
   * Starts (or reuses) an indexing request via {@link IndexRequestService}, which owns the
   * lifecycle for both this facade and the one_d4 REST API. Single-month requests run inline so the
   * caller gets a final status in one round trip; multi-month requests are enqueued and can be
   * polled via {@link #status}.
   */
  public IndexResponse index(
      String player,
      String platform,
      String startMonth,
      String endMonth,
      boolean excludeBullet,
      boolean skipCache) {
    try {
      return indexRequestService.submitHybrid(
          new IndexRequestService.Submission(
              player, platform, startMonth, endMonth, excludeBullet, skipCache));
    } catch (IllegalArgumentException e) {
      throw new IllegalArgumentException(toToolFieldNames(e.getMessage()), e);
    }
  }

  /**
   * IndexRequestService reports validation errors using the REST field names (player, startMonth,
   * endMonth), but the index_chess_games tool's arguments are username/start_month/end_month.
   * Translate so MCP clients are pointed at arguments that actually exist on the tool.
   */
  private static String toToolFieldNames(String message) {
    if (message == null) {
      return null;
    }
    return message
        .replaceAll("\\bplayer\\b", "username")
        .replaceAll("\\bstartMonth\\b", "start_month")
        .replaceAll("\\bendMonth\\b", "end_month");
  }

  public Optional<IndexResponse> status(UUID requestId) {
    return indexRequestService.status(requestId);
  }

  /**
   * Runs a ChessQL query over indexed games and returns rows with their motif occurrences.
   * Perspective fields (me.*, opponent.*, outcome) are resolved against {@code player}.
   */
  public List<GameFeatureRow> query(String chessql, String player, int limit) {
    ParsedQuery parsed = Parser.parse(chessql);
    CompiledQuery compiled = sqlCompiler.compile(parsed, player);
    List<GameFeature> rows = gameFeatureStore.query(compiled, limit, 0);

    List<String> gameUrls = rows.stream().map(GameFeature::gameUrl).toList();
    Map<String, Map<String, List<OccurrenceRow>>> occurrences =
        gameFeatureStore.queryOccurrences(gameUrls);

    return rows.stream()
        .map(
            row -> GameFeatureRow.fromStore(row, occurrences.getOrDefault(row.gameUrl(), Map.of())))
        .toList();
  }

  /**
   * Counts indexed games matching a ChessQL filter, grouped by the given fields. Perspective fields
   * in the filter are resolved against {@code player}; group-by fields must be physical columns,
   * except the perspective fields, which are groupable when {@code player} is supplied: the
   * categorical ones (me.color, me.title, opponent.username, opponent.title, outcome) by value
   * (#1301), the rating ones (me.elo, opponent.elo) as fixed-width buckets keyed by each band's
   * numeric lower bound (#1310). Alongside the (limit-truncated) groups, the result carries
   * untruncated totals so callers can tell when a long tail of groups was cut off.
   */
  public AggregateResult aggregate(String chessql, List<String> groupBy, String player, int limit) {
    ParsedQuery parsed = Parser.parse(chessql);
    List<String> groupColumns = sqlCompiler.resolveGroupByColumns(groupBy);
    CompiledQuery compiled = sqlCompiler.compileAggregate(parsed, groupBy, player);
    List<AggregateRow> groups = gameFeatureStore.aggregate(compiled, groupColumns, limit);

    // Fewer groups came back than the limit allowed, so nothing was cut off and the totals are
    // already in hand: every matching group is present, and their counts sum to every matching
    // game. Only a result that filled the limit could be hiding a tail worth a second
    // COUNT-over-groups scan.
    if (groups.size() < limit) {
      long totalGames = groups.stream().mapToLong(AggregateRow::count).sum();
      return new AggregateResult(groups, totalGames, groups.size());
    }

    CompiledQuery totalsQuery = sqlCompiler.compileAggregateTotals(parsed, groupBy, player);
    GameFeatureStore.AggregateTotals totals = gameFeatureStore.aggregateTotals(totalsQuery);
    return new AggregateResult(groups, totals.totalGames(), totals.totalGroups());
  }

  /** Aggregate result: the (possibly limit-truncated) groups plus the untruncated totals. */
  public record AggregateResult(List<AggregateRow> groups, long totalGames, long totalGroups) {}

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
}
