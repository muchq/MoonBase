package com.muchq.chess_indexer.ingest;

import com.muchq.chess_indexer.db.IndexRequestDao;
import com.muchq.chess_indexer.model.IndexRequest;
import com.muchq.chess_indexer.model.IndexRequestStatus;
import com.muchq.chess_indexer.queue.IndexRequestQueue;
import jakarta.inject.Singleton;
import java.time.LocalDate;

@Singleton
public class IndexRequestService {

  private final IndexRequestDao indexRequestDao;
  private final IndexRequestQueue queue;

  public IndexRequestService(IndexRequestDao indexRequestDao, IndexRequestQueue queue) {
    this.indexRequestDao = indexRequestDao;
    this.queue = queue;
  }

  public IndexRequest submit(String platform, String username, LocalDate startDate, LocalDate endDate) {
    IndexRequest request = indexRequestDao.create(platform, username, startDate, endDate);
    queue.enqueue(new IndexRequestQueue.IndexRequestMessage(request.id().toString()));
    indexRequestDao.updateStatus(request.id(), IndexRequestStatus.QUEUED, 0, null);
    return request;
  }
}
