-- The three core tables, in dependency order: game_features references
-- indexing_requests(id).

CREATE TABLE IF NOT EXISTS indexing_requests (
    id             UUID PRIMARY KEY DEFAULT gen_random_uuid(),
    player         VARCHAR(255) NOT NULL,
    platform       VARCHAR(50) NOT NULL,
    start_month    VARCHAR(7) NOT NULL,
    end_month      VARCHAR(7) NOT NULL,
    status         VARCHAR(20) NOT NULL DEFAULT 'PENDING',
    created_at     TIMESTAMP NOT NULL DEFAULT now(),
    updated_at     TIMESTAMP NOT NULL DEFAULT now(),
    error_message  TEXT,
    games_indexed  INT DEFAULT 0,
    exclude_bullet BOOLEAN NOT NULL DEFAULT FALSE
);

CREATE TABLE IF NOT EXISTS game_features (
    id            UUID PRIMARY KEY DEFAULT gen_random_uuid(),
    request_id    UUID NOT NULL REFERENCES indexing_requests(id),
    game_url      VARCHAR(1024) NOT NULL UNIQUE,
    platform      VARCHAR(50) NOT NULL,
    white_username VARCHAR(255),
    black_username VARCHAR(255),
    white_elo     INT,
    black_elo     INT,
    white_title   VARCHAR(10),
    black_title   VARCHAR(10),
    time_class    VARCHAR(50),
    eco           VARCHAR(10),
    opening_name  VARCHAR(255),
    opening_family VARCHAR(255),
    result        VARCHAR(20),
    played_at     TIMESTAMP,
    num_moves     INT,
    indexed_at    TIMESTAMP NOT NULL DEFAULT now(),
    pgn           TEXT
);

CREATE TABLE IF NOT EXISTS indexed_periods (
    id             UUID PRIMARY KEY DEFAULT gen_random_uuid(),
    player         VARCHAR(255) NOT NULL,
    platform       VARCHAR(50) NOT NULL,
    year_month     VARCHAR(7) NOT NULL,
    fetched_at     TIMESTAMP NOT NULL,
    is_complete    BOOLEAN NOT NULL,
    games_count    INT NOT NULL,
    exclude_bullet BOOLEAN NOT NULL DEFAULT FALSE,
    CONSTRAINT indexed_periods_unique UNIQUE (player, platform, year_month, exclude_bullet)
);
