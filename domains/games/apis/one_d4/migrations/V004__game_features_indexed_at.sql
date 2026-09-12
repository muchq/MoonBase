-- When the row was (re)written, which is what retention measures.

ALTER TABLE game_features ADD COLUMN IF NOT EXISTS indexed_at TIMESTAMP NOT NULL DEFAULT now();
