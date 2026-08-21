-- When the row was (re)written, which is what retention measures. now() is
-- valid DDL on both engines; H2 accepts it as an alias for its
-- current_timestamp().

ALTER TABLE game_features ADD COLUMN IF NOT EXISTS indexed_at TIMESTAMP NOT NULL DEFAULT now();
