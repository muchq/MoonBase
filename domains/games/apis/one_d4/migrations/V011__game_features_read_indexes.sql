-- The retention sweep's anti-join (deleteOlderThan) filters
-- indexing_requests on an hourly schedule by "does any game still point at
-- me". Without this, EXPLAIN shows a hash anti-join over a sequential scan
-- of game_features — the largest table in the schema. Neither engine
-- indexes a foreign key column automatically.

CREATE INDEX IF NOT EXISTS idx_game_features_request_id ON game_features(request_id);

-- The browse ordering. Every /v1/query without an explicit ORDER BY — the
-- browse default, including the first-load request FirstPageCache warms
-- every 30s and the page-2 prefetch the cache deliberately excludes — ends
-- in SqlCompiler's ORDER BY g.played_at DESC, g.game_url ASC LIMIT n. (An
-- explicit ORDER BY motif_count(...) sorts on a computed count and does not
-- ride this index.) Without an index in that exact column order and
-- direction the plan is a full scan plus top-N sort of the whole table per
-- page; with it, a LIMIT-sized index walk. MigrationTest pins this index
-- against the compiled default query's ORDER BY, so the two cannot drift
-- apart silently.

CREATE INDEX IF NOT EXISTS idx_game_features_played_at ON game_features(played_at DESC, game_url ASC);
