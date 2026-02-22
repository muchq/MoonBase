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
import java.time.YearMonth;
import java.time.format.DateTimeParseException;
import java.util.NoSuchElementException;
import java.util.UUID;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

@Singleton
@Path("/index")
public class IndexController {
  private static final Logger LOG = LoggerFactory.getLogger(IndexController.class);

  private final IndexingRequestStore requestDao;
  private final IndexQueue queue;

  public IndexController(IndexingRequestStore requestDao, IndexQueue queue) {
    this.requestDao = requestDao;
    this.queue = queue;
  }

  @POST
  @Consumes(MediaType.APPLICATION_JSON)
  @Produces(MediaType.APPLICATION_JSON)
  public IndexResponse createIndex(IndexRequest request) {
    validateIndexRequest(request);

    LOG.info(
        "POST /index player={} platform={} months={}-{}",
        request.player(),
        request.platform(),
        request.startMonth(),
        request.endMonth());

    UUID id =
        requestDao.create(
            request.player(), request.platform(), request.startMonth(), request.endMonth());

    queue.enqueue(
        new IndexMessage(
            id, request.player(), request.platform(), request.startMonth(), request.endMonth()));

    return new IndexResponse(id, "PENDING", 0, null);
  }

  @GET
  @Path("/{id}")
  @Produces(MediaType.APPLICATION_JSON)
  public IndexResponse getIndex(@PathParam("id") UUID id) {
    LOG.info("GET /index/{}", id);
    return requestDao
        .findById(id)
        .map(
            row ->
                new IndexResponse(row.id(), row.status(), row.gamesIndexed(), row.errorMessage()))
        .orElseThrow(() -> new NoSuchElementException("Indexing request not found: " + id));
  }

  private static void validateIndexRequest(IndexRequest request) {
    if (request.player() == null || request.player().isBlank()) {
      throw new IllegalArgumentException("player is required");
    }
    if (request.platform() == null || request.platform().isBlank()) {
      throw new IllegalArgumentException("platform is required");
    }
    if (!request.platform().equals("CHESS_COM")) {
      throw new IllegalArgumentException(
          "Unsupported platform: " + request.platform() + ". Supported: CHESS_COM");
    }
    YearMonth start = parseMonth(request.startMonth(), "startMonth");
    YearMonth end = parseMonth(request.endMonth(), "endMonth");
    if (start.isAfter(end)) {
      throw new IllegalArgumentException("startMonth must not be after endMonth");
    }
    long monthSpan = start.until(end, java.time.temporal.ChronoUnit.MONTHS) + 1;
    if (monthSpan > 12) {
      throw new IllegalArgumentException("Maximum range is 12 months, got " + monthSpan);
    }
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
