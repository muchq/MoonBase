package com.muchq.games.one_d4.api;

import com.muchq.games.one_d4.api.dto.ReanalysisResponse;
import com.muchq.games.one_d4.api.dto.RederiveResponse;
import com.muchq.games.one_d4.db.GameFeatureStore;
import com.muchq.games.one_d4.db.GameFeatureStore.GameForReanalysis;
import com.muchq.games.one_d4.db.GameFeatureStore.GameOpening;
import com.muchq.games.one_d4.engine.FeatureExtractor;
import com.muchq.games.one_d4.engine.model.GameFeatures;
import com.muchq.games.one_d4.engine.model.Motif;
import com.muchq.games.one_d4.openings.Openings;
import jakarta.inject.Singleton;
import jakarta.ws.rs.POST;
import jakarta.ws.rs.Path;
import jakarta.ws.rs.Produces;
import jakarta.ws.rs.core.MediaType;
import java.util.ArrayList;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;
import java.util.Objects;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

/**
 * Admin endpoints for operational maintenance tasks.
 *
 * <p>These endpoints are intended for internal / admin use. Phase 5 (Security) should protect them
 * with {@code ApiKeyFilter} before public exposure.
 */
@Singleton
@Path("/admin")
public class AdminController {
  private static final Logger LOG = LoggerFactory.getLogger(AdminController.class);
  static final int BATCH_SIZE = 1000;

  private final GameFeatureStore gameFeatureStore;
  private final FeatureExtractor featureExtractor;

  public AdminController(GameFeatureStore gameFeatureStore, FeatureExtractor featureExtractor) {
    this.gameFeatureStore = gameFeatureStore;
    this.featureExtractor = featureExtractor;
  }

  /**
   * Re-analyzes all games currently stored in {@code game_features}, re-running all motif detectors
   * against the stored PGN. Existing {@code motif_occurrences} rows for each game are replaced with
   * the fresh results.
   *
   * <p>Games are processed in batches of {@value #BATCH_SIZE} to bound memory usage. The endpoint
   * is synchronous — it blocks until all games have been processed and returns a summary.
   */
  @POST
  @Path("/reanalyze")
  @Produces(MediaType.APPLICATION_JSON)
  public ReanalysisResponse reanalyze() {
    LOG.info("POST /admin/reanalyze — starting full re-analysis");
    int processed = 0;
    int failed = 0;
    int offset = 0;

    List<GameForReanalysis> batch;
    do {
      batch = gameFeatureStore.fetchForReanalysis(BATCH_SIZE, offset);

      List<String> gameUrlsToDelete = new ArrayList<>();
      Map<String, Map<Motif, List<GameFeatures.MotifOccurrence>>> occurrencesBatch =
          new LinkedHashMap<>();

      for (GameForReanalysis game : batch) {
        try {
          if (game.pgn() == null || game.pgn().isBlank()) {
            LOG.warn("Skipping game with no PGN: {}", game.gameUrl());
            failed++;
            continue;
          }
          GameFeatures features = featureExtractor.extract(game.pgn());
          gameUrlsToDelete.add(game.gameUrl());
          occurrencesBatch.put(game.gameUrl(), features.occurrences());
          processed++;
        } catch (Exception e) {
          LOG.warn("Failed to reanalyze game {}", game.gameUrl(), e);
          failed++;
        }
      }

      // One transaction. A delete that commits separately from its insert leaves a window in
      // which an indexing worker flushing a live request over one of these games can insert its
      // own occurrences and have both copies survive — the doubling ConcurrentFlushTest
      // demonstrates. Being single-threaded inside this loop does not help; the other writer is
      // in another thread.
      gameFeatureStore.replaceOccurrences(gameUrlsToDelete, occurrencesBatch);

      offset += BATCH_SIZE;
      LOG.debug(
          "Re-analysis progress: processed={} failed={} offset={}", processed, failed, offset);
    } while (batch.size() == BATCH_SIZE);

    LOG.info("POST /admin/reanalyze — done: processed={} failed={}", processed, failed);
    return new ReanalysisResponse(processed, failed);
  }

  /**
   * Re-derives {@code opening_family} from the stored {@code opening_name} on every row, in place.
   *
   * <p>This exists because correcting the derivation used to mean a {@code skipCache} reindex,
   * refetching every month from chess.com to recompute something the table already determines:
   * {@code opening_family = Openings.familyFromName(opening_name)}, and the name is stored per row.
   * The network round trip bought nothing for that class of change, and it depends on archives for
   * old months still being fetchable. #1344 was the first such correction and, given the derivation
   * is a deliberately naive v1, will not be the last (#1350).
   *
   * <p>It calls the same {@link Openings} the index path calls, so the two cannot disagree about
   * what a family is — a second implementation here would be a copy that drifts on the next change,
   * which is the whole failure being corrected.
   *
   * <p>Scope is deliberately this one column. Titles come from player profiles at index time and
   * are not a function of anything stored. {@code opening_name} is a closer call: it derives from
   * the chess.com ECOUrl, which is not a column — the {@code eco} column holds the PGN's ECO code —
   * but the stored PGN usually carries the {@code [ECOUrl "..."]} tag, so a local re-derive of the
   * name is possible and simply not built here. Changing that derivation still means a reindex.
   *
   * <p>Concurrency: each write is conditional on the row still holding the name it was derived
   * from, so a game reindexed mid-pass keeps the fresher value rather than being overwritten from a
   * stale read. Rows inserted during the pass can still be missed, since paging is by offset — the
   * same property {@link #reanalyze} has, and a second run picks them up.
   *
   * <p>Only rows whose family actually changes are written, so a second run reports zero updates
   * and the number means something. Batched like {@link #reanalyze} to bound memory; synchronous.
   */
  @POST
  @Path("/rederive-openings")
  @Produces(MediaType.APPLICATION_JSON)
  public RederiveResponse rederiveOpenings() {
    LOG.info("POST /admin/rederive-openings — starting local re-derive of opening_family");
    int scanned = 0;
    int updated = 0;
    int offset = 0;

    List<GameOpening> batch;
    do {
      batch = gameFeatureStore.fetchOpeningsForRederive(BATCH_SIZE, offset);

      List<GameOpening> stale = new ArrayList<>();
      for (GameOpening game : batch) {
        scanned++;
        String derived = Openings.familyFromName(game.openingName());
        if (!Objects.equals(derived, game.openingFamily())) {
          stale.add(new GameOpening(game.gameUrl(), game.openingName(), derived));
        }
      }
      updated += gameFeatureStore.updateOpeningFamilies(stale);

      offset += BATCH_SIZE;
      LOG.debug("Re-derive progress: scanned={} updated={} offset={}", scanned, updated, offset);
    } while (batch.size() == BATCH_SIZE);

    LOG.info("POST /admin/rederive-openings — done: scanned={} updated={}", scanned, updated);
    return new RederiveResponse(scanned, updated);
  }
}
