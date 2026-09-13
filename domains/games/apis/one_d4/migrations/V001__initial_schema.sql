-- The one_d4 schema, whole. Idempotent throughout: there is no tracking
-- table, so every deploy re-runs this file. Changes are a new V<NNN> step
-- beside this one, never an edit here. See README.md.

-- The queue and its rows are one table: any worker claims from here (#1279).
-- owner_id is the fencing token every write is conditioned on.
-- lease_expires_at survives a terminal write, because it is the retention
-- sweep's only evidence that a worker was ever here. skip_cache and attempts
-- are read as primitives, so the DEFAULTs are what keep a NULL out.
CREATE TABLE IF NOT EXISTS indexing_requests (
    id               UUID PRIMARY KEY DEFAULT gen_random_uuid(),
    player           VARCHAR(255) NOT NULL,
    platform         VARCHAR(50) NOT NULL,
    start_month      VARCHAR(7) NOT NULL,
    end_month        VARCHAR(7) NOT NULL,
    status           VARCHAR(20) NOT NULL DEFAULT 'PENDING',
    created_at       TIMESTAMP NOT NULL DEFAULT now(),
    updated_at       TIMESTAMP NOT NULL DEFAULT now(),
    error_message    TEXT,
    games_indexed    INT DEFAULT 0,
    exclude_bullet   BOOLEAN NOT NULL DEFAULT FALSE,
    owner_id         VARCHAR(128),
    lease_expires_at TIMESTAMP,
    skip_cache       BOOLEAN DEFAULT FALSE,
    attempts         INT DEFAULT 0
);

-- played_at and indexed_at are TIMESTAMP without a zone. The convention the
-- type cannot carry: the stored wall clock is UTC, on both sides of every
-- comparison.
CREATE TABLE IF NOT EXISTS game_features (
    id             UUID PRIMARY KEY DEFAULT gen_random_uuid(),
    request_id     UUID NOT NULL REFERENCES indexing_requests(id),
    game_url       VARCHAR(1024) NOT NULL UNIQUE,
    platform       VARCHAR(50) NOT NULL,
    white_username VARCHAR(255),
    black_username VARCHAR(255),
    white_elo      INT,
    black_elo      INT,
    white_title    VARCHAR(10),
    black_title    VARCHAR(10),
    time_class     VARCHAR(50),
    eco            VARCHAR(10),
    opening_name   VARCHAR(255),
    opening_family VARCHAR(255),
    result         VARCHAR(20),
    played_at      TIMESTAMP,
    num_moves      INT,
    indexed_at     TIMESTAMP NOT NULL DEFAULT now(),
    pgn            TEXT
);

-- Keyed by the filter as well as the month: a month indexed without bullet
-- games answers nothing about the same month indexed with them.
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

-- One row per motif firing. The id is a UUID stored as a string, unlike
-- every other id here, so the sink casts gen_random_uuid() to text. Keyed by
-- game_url so retention's delete cascades.
CREATE TABLE IF NOT EXISTS motif_occurrences (
    id            VARCHAR(36) NOT NULL PRIMARY KEY,
    game_url      VARCHAR(1024) NOT NULL REFERENCES game_features(game_url) ON DELETE CASCADE,
    motif         VARCHAR(50) NOT NULL,
    ply           INT NOT NULL,
    side          VARCHAR(5) NOT NULL,
    move_number   INT NOT NULL,
    description   TEXT,
    moved_piece   VARCHAR(20),
    attacker      VARCHAR(20),
    target        VARCHAR(20),
    is_discovered BOOLEAN NOT NULL DEFAULT FALSE,
    is_mate       BOOLEAN NOT NULL DEFAULT FALSE,
    pin_type      VARCHAR(8)
);

-- Its own queue, deliberately (#1389 phase 5): indexers claim from
-- indexing_requests unfiltered, so a reanalysis row there is one an indexer
-- takes and cannot run. cursor_game_url is the keyset cursor, an exclusive
-- lower bound for the next page. No claimable index: one row per pass,
-- dozens a year.
CREATE TABLE IF NOT EXISTS reanalysis_requests (
    id               UUID PRIMARY KEY DEFAULT gen_random_uuid(),
    status           VARCHAR(20) NOT NULL DEFAULT 'PENDING',
    created_at       TIMESTAMP NOT NULL DEFAULT now(),
    updated_at       TIMESTAMP NOT NULL DEFAULT now(),
    owner_id         VARCHAR(128),
    lease_expires_at TIMESTAMP,
    attempts         INT NOT NULL DEFAULT 0,
    error_message    TEXT,
    cursor_game_url  VARCHAR(1024),
    games_processed  INT NOT NULL DEFAULT 0,
    games_failed     INT NOT NULL DEFAULT 0
);

-- At most one live request per range, refused at insert (#1249). A terminal
-- status drops the row out of the predicate and releases the range, so no
-- writer has to clear anything by hand.
CREATE UNIQUE INDEX IF NOT EXISTS idx_indexing_requests_live
    ON indexing_requests (player, platform, start_month, end_month, exclude_bullet)
    WHERE status IN ('PENDING', 'PROCESSING');

-- What claimNext scans, every few seconds, oldest first because the queue it
-- replaces was FIFO. Partial rather than a composite (status, created_at):
-- before PG17 a btree cannot emit ordered output under the ScalarArrayOp
-- that status IN (...) compiles to, so the composite is never chosen. CI
-- runs postgres:18, where it would have looked healthy.
CREATE INDEX IF NOT EXISTS idx_indexing_requests_claimable
    ON indexing_requests (created_at) WHERE status IN ('PENDING', 'PROCESSING');

-- The reclaim and orphan sweeps both filter on status plus a lease column.
CREATE INDEX IF NOT EXISTS idx_indexing_requests_lease
    ON indexing_requests (status, lease_expires_at);

-- At most one live pass: unique over a constant, partial over liveness. Two
-- would walk the whole corpus twice. Terminal rows fall outside and
-- accumulate.
CREATE UNIQUE INDEX IF NOT EXISTS idx_reanalysis_requests_single_live
    ON reanalysis_requests ((true)) WHERE status IN ('PENDING', 'PROCESSING');

-- Retention's hourly anti-join on "does any game still point at me".
-- Postgres does not index a foreign key column automatically.
CREATE INDEX IF NOT EXISTS idx_game_features_request_id
    ON game_features (request_id);

-- The browse default's ORDER BY played_at DESC, game_url ASC. Wrong column
-- order or direction and every page is a full scan plus top-N sort.
-- MigrationTest pins this against the compiled query so they cannot drift.
CREATE INDEX IF NOT EXISTS idx_game_features_played_at
    ON game_features (played_at DESC, game_url ASC);

-- The participation guard case-folds both sides, so only a LOWER(...) index
-- serves it. One per side: an OR across two columns needs two indexes.
CREATE INDEX IF NOT EXISTS idx_game_features_white_username
    ON game_features (LOWER(white_username));

CREATE INDEX IF NOT EXISTS idx_game_features_black_username
    ON game_features (LOWER(black_username));

-- Retention's hourly delete on indexed_at (#1313 item 11). Unindexed, a
-- sweep that hit its 120s bound rolled back having never advanced.
CREATE INDEX IF NOT EXISTS idx_game_features_indexed_at
    ON game_features (indexed_at);

CREATE INDEX IF NOT EXISTS idx_motif_occ_game_url ON motif_occurrences (game_url);

CREATE INDEX IF NOT EXISTS idx_motif_occ_motif ON motif_occurrences (motif);

CREATE INDEX IF NOT EXISTS idx_motif_occ_ply ON motif_occurrences (game_url, ply);
