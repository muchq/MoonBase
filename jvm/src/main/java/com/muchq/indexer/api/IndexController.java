package com.muchq.indexer.api;

import com.muchq.indexer.api.dto.IndexRequest;
import com.muchq.indexer.api.dto.IndexResponse;
import com.muchq.indexer.db.IndexingRequestStore;
import com.muchq.indexer.queue.IndexMessage;
import com.muchq.indexer.queue.IndexQueue;
import jakarta.ws.rs.Consumes;
import jakarta.ws.rs.GET;
import jakarta.ws.rs.POST;
import jakarta.ws.rs.Path;
import jakarta.ws.rs.PathParam;
import jakarta.ws.rs.Produces;
import jakarta.ws.rs.core.MediaType;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

import java.util.UUID;

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
        LOG.info("POST /index player={} platform={} months={}-{}",
                request.player(), request.platform(), request.startMonth(), request.endMonth());

        UUID id = requestDao.create(
                request.player(),
                request.platform(),
                request.startMonth(),
                request.endMonth()
        );

        queue.enqueue(new IndexMessage(
                id,
                request.player(),
                request.platform(),
                request.startMonth(),
                request.endMonth()
        ));

        return new IndexResponse(id, "PENDING", 0, null);
    }

    @GET
    @Path("/{id}")
    @Produces(MediaType.APPLICATION_JSON)
    public IndexResponse getIndex(@PathParam("id") UUID id) {
        LOG.info("GET /index/{}", id);
        return requestDao.findById(id)
                .map(row -> new IndexResponse(row.id(), row.status(), row.gamesIndexed(), row.errorMessage()))
                .orElseThrow(() -> new RuntimeException("Indexing request not found: " + id));
    }
}
