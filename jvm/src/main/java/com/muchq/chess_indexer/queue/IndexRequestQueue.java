package com.muchq.chess_indexer.queue;

import java.util.List;

public interface IndexRequestQueue {
  void enqueue(IndexRequestMessage message);

  List<QueuedMessage> poll(int maxMessages, int waitSeconds);

  void delete(QueuedMessage message);

  record IndexRequestMessage(String requestId) {}

  record QueuedMessage(String receiptHandle, String body) {}
}
