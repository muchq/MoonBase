package com.muchq.chess_indexer.queue;

import com.fasterxml.jackson.core.JsonProcessingException;
import com.fasterxml.jackson.databind.ObjectMapper;
import com.muchq.chess_indexer.config.IndexerConfig;
import jakarta.inject.Singleton;
import java.util.ArrayList;
import java.util.List;
import software.amazon.awssdk.services.sqs.SqsClient;
import software.amazon.awssdk.services.sqs.model.DeleteMessageRequest;
import software.amazon.awssdk.services.sqs.model.Message;
import software.amazon.awssdk.services.sqs.model.ReceiveMessageRequest;
import software.amazon.awssdk.services.sqs.model.SendMessageRequest;

@Singleton
public class SqsIndexRequestQueue implements IndexRequestQueue {

  private final SqsClient sqsClient;
  private final IndexerConfig config;
  private final ObjectMapper objectMapper;

  public SqsIndexRequestQueue(SqsClient sqsClient, IndexerConfig config, ObjectMapper objectMapper) {
    this.sqsClient = sqsClient;
    this.config = config;
    this.objectMapper = objectMapper;
  }

  @Override
  public void enqueue(IndexRequestMessage message) {
    try {
      String body = objectMapper.writeValueAsString(message);
      sqsClient.sendMessage(SendMessageRequest.builder()
          .queueUrl(config.sqsQueueUrl())
          .messageBody(body)
          .build());
    } catch (JsonProcessingException e) {
      throw new RuntimeException("Failed to serialize queue message", e);
    }
  }

  @Override
  public List<QueuedMessage> poll(int maxMessages, int waitSeconds) {
    ReceiveMessageRequest request = ReceiveMessageRequest.builder()
        .queueUrl(config.sqsQueueUrl())
        .maxNumberOfMessages(maxMessages)
        .waitTimeSeconds(waitSeconds)
        .build();

    List<Message> messages = sqsClient.receiveMessage(request).messages();
    List<QueuedMessage> queued = new ArrayList<>(messages.size());
    for (Message message : messages) {
      queued.add(new QueuedMessage(message.receiptHandle(), message.body()));
    }
    return queued;
  }

  @Override
  public void delete(QueuedMessage message) {
    sqsClient.deleteMessage(DeleteMessageRequest.builder()
        .queueUrl(config.sqsQueueUrl())
        .receiptHandle(message.receiptHandle())
        .build());
  }
}
