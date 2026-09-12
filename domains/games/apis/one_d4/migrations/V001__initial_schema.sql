-- The one_d4 schema, whole. Applied by one_d4_migrate before the services
-- start, verified at boot, and built fresh by every test suite.
--
-- Every statement is idempotent because there is no tracking table: the
-- deploy re-runs this file in full each time, and that is the mechanism.
-- Adding to the schema means a new V<NNN> step beside this one, never an
-- edit that a deployed database has already run past — see README.md.

-- The queue and its rows are one table: a submit writes the row, and any
-- worker claims it from here (#1279). owner_id is the fencing token every
-- write is conditioned on; lease_expires_at bounds a claim, and outlives a
-- terminal write on purpose — it is the only durable record that a worker
-- held this row, which the retention sweep's fleet-liveness probe needs
-- because a successful run clears the owner on its way out.
--
-- skip_cache is persisted rather than carried in a message because it stops
-- being the submitter's business the moment any worker can pick the row up.
-- attempts bounds the requeue loop for a request that keeps killing its
-- worker; it counts claims, not failures, which is the conservative
-- direction. Both are read as primitives, so their DEFAULTs are what keep a
-- NULL from silently arriving as the same values.
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

-- played_at and indexed_at are TIMESTAMP without a zone, and the convention
-- the type cannot carry is that the stored wall clock is UTC. Both sides of
-- every comparison bind through that convention, so a row indexed under one
-- zone and queried under another lands on the same calendar day.
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

-- The period cache, keyed by the filter as well as the month: a month
-- indexed without bullet games answers nothing about the same month indexed
-- with them.
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

-- One row per motif firing per game. The id is a UUID stored as a string,
-- unlike every other id here, which is why the sink generates it with
-- gen_random_uuid() cast to text. Keyed to the game by game_url rather than
-- by id, so retention's delete of a game takes its occurrences with it.
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

-- Reanalysis is its own queue, deliberately (#1389 phase 5): the indexers
-- claim from indexing_requests unfiltered, so a reanalysis row there is one
-- an indexer takes, cannot run, and fails. Separate tables make that
-- unreachable rather than a filter every present and future poller has to
-- remember.
--
-- cursor_game_url is the keyset cursor — the last game_url a completed page
-- covered, an exclusive lower bound for the next. It replaces the OFFSET
-- paging the admin passes used, which skipped rows inserted mid-pass.
--
-- No claimable index: this table takes one row per pass, dozens a year, so
-- the partial index indexing_requests needs would here cost writes to serve
-- a sequential scan of a table that fits in a page.
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

-- At most one live request per range, refused at insert. This is the whole
-- dedupe mechanism: a terminal status drops the row out of the predicate and
-- releases the range, so no writer has to remember to clear anything, and
-- the check-then-act race in IndexRequestService (#1249) cannot reopen.
-- Terminal rows fall outside the predicate and accumulate freely.
CREATE UNIQUE INDEX IF NOT EXISTS idx_indexing_requests_live
    ON indexing_requests (player, platform, start_month, end_month, exclude_bullet)
    WHERE status IN ('PENDING', 'PROCESSING');

-- What claimNext scans: the oldest live row nobody currently holds, on a
-- query every instance runs every few seconds. Ordered by created_at because
-- the queue it replaces was FIFO, and a poller that skipped ahead would
-- starve the front under sustained load.
--
-- Partial, and that is not a preference. Postgres before 17 cannot emit
-- btree output already ordered on a trailing column when the leading one
-- sits under a ScalarArrayOp, which status IN (...) is — so a composite
-- (status, created_at) is never chosen, and forcing it still produces a full
-- top-N sort of every live row. Measured at 200k rows with 10k live, the
-- partial index planned three orders of magnitude cheaper. Worth pinning
-- explicitly because CI runs postgres:18, where ordered SAOP scans do exist
-- and the composite would have looked perfectly healthy.
CREATE INDEX IF NOT EXISTS idx_indexing_requests_claimable
    ON indexing_requests (created_at) WHERE status IN ('PENDING', 'PROCESSING');

-- The reclaim sweep looks for live rows whose lease has run out, and the
-- orphan sweep for live rows that never had one. Both filter on status plus
-- a lease column every hour.
CREATE INDEX IF NOT EXISTS idx_indexing_requests_lease
    ON indexing_requests (status, lease_expires_at);

-- At most one live reanalysis pass, refused at insert. Unique over a
-- constant expression, partial over liveness: while any PENDING or
-- PROCESSING row exists, a second insert violates. Two PENDING rows are two
-- claimable passes, and every worker replica polls this table, so two of
-- them would walk the whole corpus twice for no benefit.
CREATE UNIQUE INDEX IF NOT EXISTS idx_reanalysis_requests_single_live
    ON reanalysis_requests ((true)) WHERE status IN ('PENDING', 'PROCESSING');

-- The retention sweep's anti-join filters indexing_requests hourly by "does
-- any game still point at me". Without this, EXPLAIN shows a hash anti-join
-- over a sequential scan of game_features, the largest table here. Postgres
-- does not index a foreign key column automatically.
CREATE INDEX IF NOT EXISTS idx_game_features_request_id
    ON game_features (request_id);

-- The browse ordering. Every /v1/query without an explicit ORDER BY — the
-- browse default, including the first-load request FirstPageCache warms
-- every 30s and the page-2 prefetch the cache deliberately excludes — ends
-- in SqlCompiler's ORDER BY g.played_at DESC, g.game_url ASC LIMIT n.
-- Without an index in that exact column order and direction the plan is a
-- full scan plus top-N sort of the whole table per page; with it, a
-- LIMIT-sized index walk. MigrationTest pins this index against the compiled
-- default query's ORDER BY, so the two cannot drift apart silently.
CREATE INDEX IF NOT EXISTS idx_game_features_played_at
    ON game_features (played_at DESC, game_url ASC);

-- The participation guard case-folds both sides — LOWER(white_username) =
-- LOWER(?) — so only an expression index on LOWER(...) can serve it. One per
-- side: an OR across two columns is served by two indexes, not one.
CREATE INDEX IF NOT EXISTS idx_game_features_white_username
    ON game_features (LOWER(white_username));

CREATE INDEX IF NOT EXISTS idx_game_features_black_username
    ON game_features (LOWER(black_username));

-- The retention delete (#1313 item 11) filters game_features on indexed_at
-- hourly. Unindexed, every 120s-bounded sweep re-scanned the table from the
-- start, so a sweep that hit its bound rolled back having made no forward
-- progress and retried the same scan an hour later — a ratchet that never
-- advances.
CREATE INDEX IF NOT EXISTS idx_game_features_indexed_at
    ON game_features (indexed_at);

CREATE INDEX IF NOT EXISTS idx_motif_occ_game_url ON motif_occurrences (game_url);

CREATE INDEX IF NOT EXISTS idx_motif_occ_motif ON motif_occurrences (motif);

CREATE INDEX IF NOT EXISTS idx_motif_occ_ply ON motif_occurrences (game_url, ply);
