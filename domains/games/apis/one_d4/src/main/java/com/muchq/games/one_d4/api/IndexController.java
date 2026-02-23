package com.muchq.games.one_d4.api;

import com.muchq.games.one_d4.api.dto.IndexRequest;
import com.muchq.games.one_d4.api.dto.IndexResponse;
import com.muchq.games.one_d4.db.IndexingRequestStore;
import com.muchq.games.one_d4.queue.IndexMessage;
import com.muchq.games.one_d4.queue.IndexQueue;
import jakarta.inject.Singleton;
import jakarta.ws.rs.Consumes;
import jakarta.ws.rs.GET;
import jakarta.ws.rs.POST;
import jakarta.ws.rs.Path;
import jakarta.ws.rs.PathParam;
import jakarta.ws.rs.Produces;
import jakarta.ws.rs.core.MediaType;
import java.util.NoSuchElementException;
import java.util.Optional;
import java.util.UUID;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

@Singleton
@Path("/v1/index")
public class IndexController {
  private static final Logger LOG = LoggerFactory.getLogger(IndexController.class);

  private final IndexingRequestStore requestDao;
  private final IndexQueue queue;
  private final IndexRequestValidator validator;

  public IndexController(
      IndexingRequestStore requestDao, IndexQueue queue, IndexRequestValidator validator) {
    this.requestDao = requestDao;
    this.queue = queue;
    this.validator = validator;
  }

  @POST
  @Consumes(MediaType.APPLICATION_JSON)
  @Produces(MediaType.APPLICATION_JSON)
  public IndexResponse createIndex(IndexRequest request) {
    validator.validate(request);

    LOG.info(
        "POST /v1/index player={} platform={} months={}-{}",
        request.player(),
        request.platform(),
        request.startMonth(),
        request.endMonth());

    Optional<IndexingRequestStore.IndexingRequest> existing =
        requestDao.findExistingRequest(
            request.player(), request.platform(), request.startMonth(), request.endMonth());
    if (existing.isPresent()) {
      IndexingRequestStore.IndexingRequest row = existing.get();
      LOG.info("Returning existing index request {} (status={})", row.id(), row.status());
      return new IndexResponse(
          row.id(),
          row.player(),
          row.platform(),
          row.startMonth(),
          row.endMonth(),
          row.status(),
          row.gamesIndexed(),
          row.errorMessage());
    }

    UUID id =
        requestDao.create(
            request.player(), request.platform(), request.startMonth(), request.endMonth());

    queue.enqueue(
        new IndexMessage(
            id, request.player(), request.platform(), request.startMonth(), request.endMonth()));

    return new IndexResponse(
        id,
        request.player(),
        request.platform(),
        request.startMonth(),
        request.endMonth(),
        "PENDING",
        0,
        null);
  }

  @GET
  @Path("/{id}")
  @Produces(MediaType.APPLICATION_JSON)
  public IndexResponse getIndex(@PathParam("id") UUID id) {
    LOG.info("GET /v1/index/{}", id);
    return requestDao
        .findById(id)
        .map(
            row ->
                new IndexResponse(
                    row.id(),
                    row.player(),
                    row.platform(),
                    row.startMonth(),
                    row.endMonth(),
                    row.status(),
                    row.gamesIndexed(),
                    row.errorMessage()))
        .orElseThrow(() -> new NoSuchElementException("Indexing request not found: " + id));
  }
}
