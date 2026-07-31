package com.muchq.games.one_d4.api;

import com.muchq.games.one_d4.api.dto.DataAvailability;
import com.muchq.games.one_d4.api.dto.IndexRequest;
import com.muchq.games.one_d4.api.dto.IndexResponse;
import com.muchq.games.one_d4.db.IndexingRequestStore;
import com.muchq.games.one_d4.service.DataAvailabilityResolver;
import com.muchq.games.one_d4.service.IndexRequestService;
import jakarta.inject.Singleton;
import jakarta.ws.rs.Consumes;
import jakarta.ws.rs.GET;
import jakarta.ws.rs.POST;
import jakarta.ws.rs.Path;
import jakarta.ws.rs.PathParam;
import jakarta.ws.rs.Produces;
import jakarta.ws.rs.core.MediaType;
import java.util.List;
import java.util.Map;
import java.util.NoSuchElementException;
import java.util.UUID;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

/** Thin HTTP adapter over {@link IndexRequestService}, which owns the request lifecycle. */
@Singleton
@Path("/v1/index")
public class IndexController {
  private static final Logger LOG = LoggerFactory.getLogger(IndexController.class);

  private final IndexRequestService indexRequestService;
  private final IndexingRequestStore requestDao;
  private final DataAvailabilityResolver dataAvailability;

  public IndexController(
      IndexRequestService indexRequestService,
      IndexingRequestStore requestDao,
      DataAvailabilityResolver dataAvailability) {
    this.indexRequestService = indexRequestService;
    this.requestDao = requestDao;
    this.dataAvailability = dataAvailability;
  }

  @GET
  @Produces(MediaType.APPLICATION_JSON)
  public List<IndexResponse> listRequests() {
    LOG.info("GET /v1/index");
    List<IndexingRequestStore.IndexingRequest> rows = requestDao.listRecent(50);
    // One storage lookup for the whole page: request rows outlive their games, so without this
    // a swept request is indistinguishable from a fresh one.
    Map<UUID, DataAvailability> availability = dataAvailability.resolveAll(rows);
    return rows.stream()
        .map(row -> IndexRequestService.toResponse(row).withData(availability.get(row.id())))
        .toList();
  }

  @POST
  @Consumes(MediaType.APPLICATION_JSON)
  @Produces(MediaType.APPLICATION_JSON)
  public IndexResponse createIndex(IndexRequest request) {
    LOG.info(
        "POST /v1/index player={} platform={} months={}-{} excludeBullet={} skipCache={}",
        request.player(),
        request.platform(),
        request.startMonth(),
        request.endMonth(),
        request.excludeBullet(),
        request.skipCache());

    return indexRequestService.submit(
        new IndexRequestService.Submission(
            request.player(),
            request.platform(),
            request.startMonth(),
            request.endMonth(),
            Boolean.TRUE.equals(request.excludeBullet()),
            Boolean.TRUE.equals(request.skipCache())));
  }

  @GET
  @Path("/{id}")
  @Produces(MediaType.APPLICATION_JSON)
  public IndexResponse getIndex(@PathParam("id") UUID id) {
    LOG.info("GET /v1/index/{}", id);
    return indexRequestService
        .status(id)
        .orElseThrow(() -> new NoSuchElementException("Indexing request not found: " + id));
  }
}
