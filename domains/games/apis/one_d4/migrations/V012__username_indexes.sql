-- The indexes behind username search (#1313 item 10). Two compiler paths
-- emit the same case-folded predicate shape, LOWER(white_username) =
-- LOWER(?) OR'd across the sides: the browse UI's username search compiles
-- white.username = "x" OR black.username = "x" through the STRING_COLUMNS
-- equality branch — the highest-traffic consumer — and every
-- perspective-field query runs the participation guard. Case-folded on both
-- sides, so a plain column index can never serve either on Postgres: these
-- are expression indexes on LOWER(...). One per side rather than any
-- composite — an OR across two columns is answered by a BitmapOr of two
-- independent scans, and a BitmapOr's output is unordered, so a composite
-- with played_at could not skip the sort either; it would only double index
-- weight on the hottest-write table.
--
-- Without them these predicates were the full-table scan on the
-- player-search path — and with a 5-connection pool, five concurrent player
-- searches held every connection for the full serving-read bound.
-- PostgresPlayerIndexTest pins the contract on the deployment dialect for
-- both emitting paths: the plan actually reaches these indexes for the
-- compiler's exact predicates, so either side drifting (a compiler path
-- losing its LOWER, or an index expression changing) fails a test.
--
-- Ops note, same as every index here: created without CONCURRENTLY, so the
-- first run onto a populated table holds a SHARE lock for the build and
-- pauses the indexer's writes; later runs are IF NOT EXISTS no-ops.
-- (CONCURRENTLY + IF NOT EXISTS would be worse — a failed build leaves an
-- INVALID index behind that IF NOT EXISTS then skips forever.)

CREATE INDEX IF NOT EXISTS idx_game_features_white_username ON game_features(LOWER(white_username));

CREATE INDEX IF NOT EXISTS idx_game_features_black_username ON game_features(LOWER(black_username));
