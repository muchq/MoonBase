package com.muchq.chess_indexer.config;

public record IndexerConfig(
    String dbUrl,
    String dbUser,
    String dbPassword,
    String sqsQueueUrl,
    String awsRegion,
    int workerPollSeconds,
    int workerBatchSize,
    int apiQueryLimit
) {

  public static IndexerConfig fromEnv() {
    String dbUrl = getenvOrThrow("INDEXER_DB_URL");
    String dbUser = getenvOrDefault("INDEXER_DB_USER", "postgres");
    String dbPassword = getenvOrDefault("INDEXER_DB_PASSWORD", "");
    String sqsQueueUrl = getenvOrDefault("INDEXER_SQS_QUEUE_URL", "");
    String awsRegion = getenvOrDefault("AWS_REGION", "us-east-1");

    int workerPollSeconds = parseInt(getenvOrDefault("INDEXER_WORKER_POLL_SECONDS", "10"));
    int workerBatchSize = parseInt(getenvOrDefault("INDEXER_WORKER_BATCH_SIZE", "5"));
    int apiQueryLimit = parseInt(getenvOrDefault("INDEXER_API_QUERY_LIMIT", "100"));

    return new IndexerConfig(
        dbUrl,
        dbUser,
        dbPassword,
        sqsQueueUrl,
        awsRegion,
        workerPollSeconds,
        workerBatchSize,
        apiQueryLimit);
  }

  private static String getenvOrDefault(String key, String defaultValue) {
    String value = System.getenv(key);
    return value == null ? defaultValue : value;
  }

  private static String getenvOrThrow(String key) {
    String value = System.getenv(key);
    if (value == null || value.isBlank()) {
      throw new IllegalStateException("Missing required env var: " + key);
    }
    return value;
  }

  private static int parseInt(String raw) {
    try {
      return Integer.parseInt(raw);
    } catch (NumberFormatException e) {
      throw new IllegalStateException("Failed to parse int env var: " + raw, e);
    }
  }
}
