-- The reanalysis queue (#1389 phase 5). Same claim/lease/fence shape as
-- indexing_requests and deliberately not the same table: the indexers claim
-- from that one unfiltered, so a reanalysis row there is one an indexer
-- takes, cannot run, and fails. Separate tables make that unreachable
-- rather than a filter every present and future poller has to remember.
--
-- No claimable index. This table takes one row per reanalysis pass — dozens
-- a year, not the hundreds of thousands indexing_requests holds — so the
-- partial index that one needs would here cost writes to serve a sequential
-- scan of a table that fits in a page.
--
-- cursor_game_url is the keyset cursor: the last game_url a completed page
-- covered, an exclusive lower bound for the next. Replaces the OFFSET
-- paging both admin passes used, which skipped rows inserted mid-pass and
-- needed a second run to catch them.

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

-- At most one live reanalysis pass, refused at insert. Unique over a
-- constant expression, partial over liveness: while any PENDING or
-- PROCESSING row exists, a second insert violates. Two PENDING rows are two
-- claimable passes — every worker replica polls this table, and two of them
-- would walk the whole corpus twice for no benefit. History rows
-- (COMPLETED, FAILED) fall outside the predicate and accumulate freely.

CREATE UNIQUE INDEX IF NOT EXISTS idx_reanalysis_requests_single_live ON reanalysis_requests ((true)) WHERE status IN ('PENDING', 'PROCESSING');
