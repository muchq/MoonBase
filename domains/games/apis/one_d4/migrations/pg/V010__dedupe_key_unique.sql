-- The live-request dedupe constraint (see V009). Postgres has no
-- ADD CONSTRAINT IF NOT EXISTS, so the DO block swallows the
-- already-exists errors on re-runs.

DO $$ BEGIN
  ALTER TABLE indexing_requests ADD CONSTRAINT indexing_requests_dedupe_unique
    UNIQUE (dedupe_key);
EXCEPTION WHEN duplicate_table OR duplicate_object THEN NULL;
END $$;
