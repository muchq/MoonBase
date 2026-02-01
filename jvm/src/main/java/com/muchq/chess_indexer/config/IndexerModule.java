package com.muchq.chess_indexer.config;

import com.fasterxml.jackson.databind.ObjectMapper;
import com.muchq.chess_com_api.ChessClient;
import com.muchq.http_client.core.HttpClient;
import com.muchq.http_client.jdk11.Jdk11HttpClient;
import com.muchq.json.JsonUtils;
import com.zaxxer.hikari.HikariConfig;
import com.zaxxer.hikari.HikariDataSource;
import io.micronaut.context.annotation.Context;
import io.micronaut.context.annotation.Factory;
import jakarta.inject.Singleton;
import java.time.Clock;
import javax.sql.DataSource;
import software.amazon.awssdk.auth.credentials.DefaultCredentialsProvider;
import software.amazon.awssdk.regions.Region;
import software.amazon.awssdk.services.sqs.SqsClient;
import software.amazon.awssdk.http.urlconnection.UrlConnectionHttpClient;

@Factory
public class IndexerModule {

  @Context
  public IndexerConfig indexerConfig() {
    return IndexerConfig.fromEnv();
  }

  @Context
  public Clock clock() {
    return Clock.systemUTC();
  }

  @Context
  public ObjectMapper objectMapper() {
    return JsonUtils.mapper();
  }

  @Context
  public HttpClient httpClient() {
    return new Jdk11HttpClient(java.net.http.HttpClient.newHttpClient());
  }

  @Context
  public ChessClient chessClient(HttpClient httpClient, ObjectMapper objectMapper) {
    return new ChessClient(httpClient, objectMapper);
  }

  @Singleton
  public DataSource dataSource(IndexerConfig config) {
    HikariConfig hikari = new HikariConfig();
    hikari.setJdbcUrl(config.dbUrl());
    hikari.setUsername(config.dbUser());
    hikari.setPassword(config.dbPassword());
    hikari.setMaximumPoolSize(10);
    hikari.setMinimumIdle(1);
    hikari.setPoolName("chess-indexer");
    return new HikariDataSource(hikari);
  }

  @Singleton
  public SqsClient sqsClient(IndexerConfig config) {
    return SqsClient.builder()
        .region(Region.of(config.awsRegion()))
        .credentialsProvider(DefaultCredentialsProvider.create())
        .httpClientBuilder(UrlConnectionHttpClient.builder())
        .build();
  }
}
