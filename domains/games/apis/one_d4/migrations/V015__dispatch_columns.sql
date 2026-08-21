-- Columns that make the table dispatchable (#1279).
--
-- skip_cache has to be persisted because it stops being the submitter's
-- business the moment any worker can pick the row up. Every other field a
-- run needs is already a column; this one lived only in the queue message,
-- so a worker claiming from the table would silently honour the period
-- cache for a request that asked to bypass it.
--
-- attempts is the bound that replaces "nobody ever picked this up". Once an
-- expired lease returns work to the queue instead of retiring it, a request
-- that reliably kills its worker is retried forever, across every instance
-- — a possibility that did not exist while a crashed process took its queue
-- with it. Incremented on each claim, so it counts attempts and not
-- failures, which is the conservative direction: a worker killed before it
-- could report anything still moves the counter.
--
-- The DEFAULTs are the whole backfill — both engines fill existing rows
-- from them as the column is added, so a request in flight during a deploy
-- comes out as "do not skip the cache, never attempted", which is exactly
-- its pre-#1279 behaviour. Both columns are read as primitives (getBoolean,
-- getInt), so a NULL would silently arrive as the same values without
-- anyone having decided that; the DEFAULT is what keeps that from being
-- load-bearing.

ALTER TABLE indexing_requests ADD COLUMN IF NOT EXISTS skip_cache BOOLEAN DEFAULT FALSE;

ALTER TABLE indexing_requests ADD COLUMN IF NOT EXISTS attempts INT DEFAULT 0;
