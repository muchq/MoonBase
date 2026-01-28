package com.muchq.indexer.db;

import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

import javax.sql.DataSource;
import java.sql.Connection;
import java.sql.SQLException;
import java.sql.Statement;

public class Migration {
    private static final Logger LOG = LoggerFactory.getLogger(Migration.class);

    private final DataSource dataSource;

    public Migration(DataSource dataSource) {
        this.dataSource = dataSource;
    }

    public void run() {
        try (Connection conn = dataSource.getConnection();
             Statement stmt = conn.createStatement()) {

            stmt.execute("""
                CREATE TABLE IF NOT EXISTS indexing_requests (
                    id            UUID PRIMARY KEY DEFAULT gen_random_uuid(),
                    player        VARCHAR(255) NOT NULL,
                    platform      VARCHAR(50) NOT NULL,
                    start_month   VARCHAR(7) NOT NULL,
                    end_month     VARCHAR(7) NOT NULL,
                    status        VARCHAR(20) NOT NULL DEFAULT 'PENDING',
                    created_at    TIMESTAMP NOT NULL DEFAULT now(),
                    updated_at    TIMESTAMP NOT NULL DEFAULT now(),
                    error_message TEXT,
                    games_indexed INT DEFAULT 0
                )
                """);

            stmt.execute("""
                CREATE TABLE IF NOT EXISTS game_features (
                    id            UUID PRIMARY KEY DEFAULT gen_random_uuid(),
                    request_id    UUID NOT NULL REFERENCES indexing_requests(id),
                    game_url      VARCHAR(1024) NOT NULL UNIQUE,
                    platform      VARCHAR(50) NOT NULL,
                    white_username VARCHAR(255),
                    black_username VARCHAR(255),
                    white_elo     INT,
                    black_elo     INT,
                    time_class    VARCHAR(50),
                    eco           VARCHAR(10),
                    result        VARCHAR(20),
                    played_at     TIMESTAMP,
                    num_moves     INT,
                    has_pin       BOOLEAN DEFAULT FALSE,
                    has_cross_pin BOOLEAN DEFAULT FALSE,
                    has_fork      BOOLEAN DEFAULT FALSE,
                    has_skewer    BOOLEAN DEFAULT FALSE,
                    has_discovered_attack BOOLEAN DEFAULT FALSE,
                    motifs_json   JSONB,
                    pgn           TEXT
                )
                """);

            LOG.info("Database migration completed successfully");
        } catch (SQLException e) {
            throw new RuntimeException("Failed to run database migration", e);
        }
    }
}
