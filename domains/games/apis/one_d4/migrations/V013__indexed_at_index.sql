-- The index behind the retention delete (#1313 item 11). deleteOlderThan
-- filters game_features on indexed_at hourly; unindexed, every 120s-bounded
-- sweep re-scanned the table from the start, so a sweep that hit its bound
-- rolled back having made no forward progress and retried the same scan an
-- hour later — a ratchet that never advances. The delete is one statement,
-- so a cancel always rolls the whole pass back; what the index changes is
-- the cost side, letting the steady-state delete walk straight to the
-- expired rows and finish inside the bound. (If a backlog ever outgrows the
-- bound anyway, LIMIT-batched deletes are the next tool.) Dialect-neutral:
-- a plain b-tree on a plain column.

CREATE INDEX IF NOT EXISTS idx_game_features_indexed_at ON game_features(indexed_at);
