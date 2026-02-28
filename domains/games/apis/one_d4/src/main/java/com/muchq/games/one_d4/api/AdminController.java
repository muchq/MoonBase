package com.muchq.games.one_d4.api;

import com.muchq.games.one_d4.api.dto.ReanalysisResponse;
import com.muchq.games.one_d4.db.GameFeatureStore;
import com.muchq.games.one_d4.db.GameFeatureStore.GameForReanalysis;
import com.muchq.games.one_d4.engine.FeatureExtractor;
import com.muchq.games.one_d4.engine.model.GameFeatures;
import jakarta.inject.Singleton;
import jakarta.ws.rs.POST;
import jakarta.ws.rs.Path;
import jakarta.ws.rs.Produces;
import jakarta.ws.rs.core.MediaType;
import java.util.List;
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
  private static final int BATCH_SIZE = 1000;

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
      for (GameForReanalysis game : batch) {
        try {
          if (game.pgn() == null || game.pgn().isBlank()) {
            LOG.warn("Skipping game with no PGN: {}", game.gameUrl());
            failed++;
            continue;
          }
          GameFeatures features = featureExtractor.extract(game.pgn());
          gameFeatureStore.deleteOccurrencesByGameUrl(game.gameUrl());
          gameFeatureStore.insertOccurrences(game.gameUrl(), features.occurrences());
          processed++;
        } catch (Exception e) {
          LOG.warn("Failed to reanalyze game {}", game.gameUrl(), e);
          failed++;
        }
      }
      offset += BATCH_SIZE;
      LOG.debug(
          "Re-analysis progress: processed={} failed={} offset={}", processed, failed, offset);
    } while (batch.size() == BATCH_SIZE);

    LOG.info("POST /admin/reanalyze — done: processed={} failed={}", processed, failed);
    return new ReanalysisResponse(processed, failed);
  }
}
