-- Opening name/family (derived from the chess.com ECOUrl) and player titles.
-- Existing rows get NULL until the affected periods are reindexed.

ALTER TABLE game_features ADD COLUMN IF NOT EXISTS white_title VARCHAR(10);

ALTER TABLE game_features ADD COLUMN IF NOT EXISTS black_title VARCHAR(10);

ALTER TABLE game_features ADD COLUMN IF NOT EXISTS opening_name VARCHAR(255);

ALTER TABLE game_features ADD COLUMN IF NOT EXISTS opening_family VARCHAR(255);
