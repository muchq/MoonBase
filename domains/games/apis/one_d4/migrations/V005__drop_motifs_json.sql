-- Drop the legacy motifs_json column, replaced by the motif_occurrences
-- table (V006).

ALTER TABLE game_features DROP COLUMN IF EXISTS motifs_json;
