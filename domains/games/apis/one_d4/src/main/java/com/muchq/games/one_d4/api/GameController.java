package com.muchq.games.one_d4.api;

import com.muchq.games.one_d4.api.dto.GameDetailRequest;
import com.muchq.games.one_d4.api.dto.GameFeatureRow;
import com.muchq.games.one_d4.api.dto.OccurrenceRow;
import com.muchq.games.one_d4.db.GameFeatureStore;
import jakarta.inject.Singleton;
import jakarta.ws.rs.Consumes;
import jakarta.ws.rs.POST;
import jakarta.ws.rs.Path;
import jakarta.ws.rs.Produces;
import jakarta.ws.rs.core.MediaType;
import jakarta.ws.rs.core.Response;
import java.util.List;
import java.util.Map;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

@Singleton
@Path("/v1/games")
public class GameController {
  private static final Logger LOG = LoggerFactory.getLogger(GameController.class);

  private final GameFeatureStore gameFeatureStore;

  public GameController(GameFeatureStore gameFeatureStore) {
    this.gameFeatureStore = gameFeatureStore;
  }

  @POST
  @Path("/detail")
  @Consumes(MediaType.APPLICATION_JSON)
  @Produces(MediaType.APPLICATION_JSON)
  public Response getDetail(GameDetailRequest request) {
    if (request == null || request.gameUrl() == null || request.gameUrl().isBlank()) {
      return Response.status(Response.Status.BAD_REQUEST).entity("gameUrl is required").build();
    }
    String gameUrl = request.gameUrl().trim();
    LOG.info("POST /v1/games/detail gameUrl={}", gameUrl);

    return gameFeatureStore
        .findByGameUrl(gameUrl)
        .map(
            feature -> {
              Map<String, List<OccurrenceRow>> occurrences =
                  gameFeatureStore
                      .queryOccurrences(List.of(gameUrl))
                      .getOrDefault(gameUrl, Map.of());
              GameFeatureRow row = GameFeatureRow.fromStore(feature, occurrences);
              return Response.ok(row).build();
            })
        .orElse(Response.status(Response.Status.NOT_FOUND).build());
  }
}
