-- The reanalysis queue (#1389 phase 5). See pg/V017 for why it is its own
-- table.

CREATE TABLE IF NOT EXISTS reanalysis_requests (
    id               UUID DEFAULT random_uuid() PRIMARY KEY,
    status           VARCHAR(20) NOT NULL DEFAULT 'PENDING',
    created_at       TIMESTAMP NOT NULL DEFAULT current_timestamp(),
    updated_at       TIMESTAMP NOT NULL DEFAULT current_timestamp(),
    owner_id         VARCHAR(128),
    lease_expires_at TIMESTAMP,
    attempts         INT NOT NULL DEFAULT 0,
    error_message    TEXT,
    cursor_game_url  VARCHAR(1024),
    games_processed  INT NOT NULL DEFAULT 0,
    games_failed     INT NOT NULL DEFAULT 0
);

-- Plain, not unique: H2 has no partial indexes, and a full unique on a
-- constant would allow one row ever. Existence keeps the migration path
-- identical; the semantics are Postgres's, asserted in
-- PostgresSingleLiveReanalysisTest.

CREATE INDEX IF NOT EXISTS idx_reanalysis_requests_single_live ON reanalysis_requests(status);
