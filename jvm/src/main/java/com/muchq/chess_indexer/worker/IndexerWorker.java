package com.muchq.chess_indexer.worker;

import com.fasterxml.jackson.databind.ObjectMapper;
import com.muchq.chess_indexer.config.IndexerConfig;
import com.muchq.chess_indexer.db.IndexRequestDao;
import com.muchq.chess_indexer.ingest.GameIngestor;
import com.muchq.chess_indexer.model.IndexRequest;
import com.muchq.chess_indexer.model.IndexRequestStatus;
import com.muchq.chess_indexer.queue.IndexRequestQueue;
import jakarta.inject.Singleton;
import java.util.List;
import java.util.Optional;
import java.util.UUID;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

@Singleton
public class IndexerWorker {

  private static final Logger LOG = LoggerFactory.getLogger(IndexerWorker.class);

  private final IndexerConfig config;
  private final IndexRequestDao indexRequestDao;
  private final IndexRequestQueue queue;
  private final GameIngestor ingestor;
  private final ObjectMapper objectMapper;

  public IndexerWorker(IndexerConfig config,
                       IndexRequestDao indexRequestDao,
                       IndexRequestQueue queue,
                       GameIngestor ingestor,
                       ObjectMapper objectMapper) {
    this.config = config;
    this.indexRequestDao = indexRequestDao;
    this.queue = queue;
    this.ingestor = ingestor;
    this.objectMapper = objectMapper;
  }

  public void runForever() {
    LOG.info("Starting indexer worker");
    while (true) {
      List<IndexRequestQueue.QueuedMessage> messages = queue.poll(
          config.workerBatchSize(),
          config.workerPollSeconds());
      if (messages.isEmpty()) {
        continue;
      }

      for (IndexRequestQueue.QueuedMessage message : messages) {
        handleMessage(message);
      }
    }
  }

  private void handleMessage(IndexRequestQueue.QueuedMessage queuedMessage) {
    IndexRequestQueue.IndexRequestMessage payload = parseMessage(queuedMessage.body());
    if (payload == null) {
      queue.delete(queuedMessage);
      return;
    }

    UUID requestId = UUID.fromString(payload.requestId());
    Optional<IndexRequest> request = indexRequestDao.findById(requestId);
    if (request.isEmpty()) {
      LOG.warn("Index request {} not found", requestId);
      queue.delete(queuedMessage);
      return;
    }

    indexRequestDao.updateStatus(requestId, IndexRequestStatus.RUNNING, 0, null);
    try {
      int gamesIndexed = ingestor.ingest(request.get());
      indexRequestDao.updateStatus(requestId, IndexRequestStatus.COMPLETED, gamesIndexed, null);
      queue.delete(queuedMessage);
    } catch (Exception e) {
      LOG.error("Failed to process index request {}", requestId, e);
      indexRequestDao.updateStatus(requestId, IndexRequestStatus.FAILED, 0, e.getMessage());
      queue.delete(queuedMessage);
    }
  }

  private IndexRequestQueue.IndexRequestMessage parseMessage(String body) {
    try {
      return objectMapper.readValue(body, IndexRequestQueue.IndexRequestMessage.class);
    } catch (Exception e) {
      LOG.error("Failed to parse queue message: {}", body, e);
      return null;
    }
  }
}
