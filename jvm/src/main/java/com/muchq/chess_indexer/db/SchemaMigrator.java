package com.muchq.chess_indexer.db;

import io.micronaut.context.event.StartupEvent;
import io.micronaut.runtime.event.annotation.EventListener;
import jakarta.inject.Singleton;
import java.sql.Connection;
import java.sql.SQLException;
import java.sql.Statement;
import javax.sql.DataSource;

@Singleton
public class SchemaMigrator {

  private final DataSource dataSource;

  public SchemaMigrator(DataSource dataSource) {
    this.dataSource = dataSource;
  }

  @EventListener
  public void onStartup(StartupEvent event) {
    migrate();
  }

  public void migrate() {
    try (Connection connection = dataSource.getConnection();
         Statement statement = connection.createStatement()) {
      statement.executeUpdate("""
          CREATE TABLE IF NOT EXISTS index_requests (
            id UUID PRIMARY KEY,
            platform TEXT NOT NULL,
            username TEXT NOT NULL,
            start_date DATE NOT NULL,
            end_date DATE NOT NULL,
            status TEXT NOT NULL,
            games_indexed INTEGER NOT NULL DEFAULT 0,
            error_message TEXT,
            created_at TIMESTAMP WITH TIME ZONE NOT NULL,
            updated_at TIMESTAMP WITH TIME ZONE NOT NULL
          );
          """);

      statement.executeUpdate("""
          CREATE TABLE IF NOT EXISTS games (
            id UUID PRIMARY KEY,
            platform TEXT NOT NULL,
            game_uuid TEXT NOT NULL,
            end_time TIMESTAMP WITH TIME ZONE NOT NULL,
            rated BOOLEAN NOT NULL,
            time_class TEXT,
            rules TEXT,
            eco TEXT,
            white_username TEXT,
            white_elo INTEGER,
            black_username TEXT,
            black_elo INTEGER,
            result TEXT,
            pgn TEXT,
            created_at TIMESTAMP WITH TIME ZONE NOT NULL
          );
          """);

      statement.executeUpdate("""
          CREATE UNIQUE INDEX IF NOT EXISTS games_platform_uuid_idx
            ON games(platform, game_uuid);
          """);

      statement.executeUpdate("""
          CREATE TABLE IF NOT EXISTS game_features (
            game_id UUID PRIMARY KEY REFERENCES games(id) ON DELETE CASCADE,
            total_plies INTEGER NOT NULL,
            has_castle BOOLEAN NOT NULL,
            has_promotion BOOLEAN NOT NULL,
            has_check BOOLEAN NOT NULL,
            has_checkmate BOOLEAN NOT NULL
          );
          """);

      statement.executeUpdate("""
          CREATE TABLE IF NOT EXISTS game_motifs (
            game_id UUID NOT NULL REFERENCES games(id) ON DELETE CASCADE,
            motif_name TEXT NOT NULL,
            first_ply INTEGER NOT NULL,
            PRIMARY KEY (game_id, motif_name)
          );
          """);

      statement.executeUpdate("""
          CREATE TABLE IF NOT EXISTS index_request_games (
            index_request_id UUID NOT NULL REFERENCES index_requests(id) ON DELETE CASCADE,
            game_id UUID NOT NULL REFERENCES games(id) ON DELETE CASCADE,
            PRIMARY KEY (index_request_id, game_id)
          );
          """);
    } catch (SQLException e) {
      throw new RuntimeException("Failed to migrate schema", e);
    }
  }
}
