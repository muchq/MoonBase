-- motif_occurrences: one row per motif firing per game. Dialect-neutral —
-- the id is a UUID stored as a string, unlike every other id in the schema,
-- which is why the sink generates it with gen_random_uuid() cast to text.
-- The first five ALTER TABLE statements after the indexes are the legacy
-- upgrade path for tables created before those columns were in the CREATE;
-- pin_type is different — it is deliberately NOT in the CREATE body, so its
-- ALTER is the only statement that ever creates it, fresh databases
-- included.

CREATE TABLE IF NOT EXISTS motif_occurrences (
    id           VARCHAR(36) NOT NULL PRIMARY KEY,
    game_url     VARCHAR(1024) NOT NULL REFERENCES game_features(game_url) ON DELETE CASCADE,
    motif        VARCHAR(50) NOT NULL,
    ply          INT NOT NULL,
    side         VARCHAR(5) NOT NULL,
    move_number  INT NOT NULL,
    description  TEXT,
    moved_piece  VARCHAR(20),
    attacker     VARCHAR(20),
    target       VARCHAR(20),
    is_discovered BOOLEAN NOT NULL DEFAULT FALSE,
    is_mate       BOOLEAN NOT NULL DEFAULT FALSE
);

CREATE INDEX IF NOT EXISTS idx_motif_occ_game_url ON motif_occurrences(game_url);

CREATE INDEX IF NOT EXISTS idx_motif_occ_motif ON motif_occurrences(motif);

CREATE INDEX IF NOT EXISTS idx_motif_occ_ply ON motif_occurrences(game_url, ply);

-- Structured fields for discovered attack/check occurrences.
ALTER TABLE motif_occurrences ADD COLUMN IF NOT EXISTS moved_piece VARCHAR(20);

ALTER TABLE motif_occurrences ADD COLUMN IF NOT EXISTS attacker VARCHAR(20);

ALTER TABLE motif_occurrences ADD COLUMN IF NOT EXISTS target VARCHAR(20);

ALTER TABLE motif_occurrences ADD COLUMN IF NOT EXISTS is_discovered BOOLEAN NOT NULL DEFAULT FALSE;

ALTER TABLE motif_occurrences ADD COLUMN IF NOT EXISTS is_mate BOOLEAN NOT NULL DEFAULT FALSE;

ALTER TABLE motif_occurrences ADD COLUMN IF NOT EXISTS pin_type VARCHAR(8);
