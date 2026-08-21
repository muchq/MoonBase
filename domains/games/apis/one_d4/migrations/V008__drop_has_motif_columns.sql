-- Drop the has_* boolean motif columns — queries use motif_occurrences
-- directly. One drop per statement because H2 doesn't support
-- comma-separated multi-column drops.

ALTER TABLE game_features DROP COLUMN IF EXISTS has_pin;

ALTER TABLE game_features DROP COLUMN IF EXISTS has_cross_pin;

ALTER TABLE game_features DROP COLUMN IF EXISTS has_fork;

ALTER TABLE game_features DROP COLUMN IF EXISTS has_skewer;

ALTER TABLE game_features DROP COLUMN IF EXISTS has_discovered_attack;

ALTER TABLE game_features DROP COLUMN IF EXISTS has_discovered_mate;

ALTER TABLE game_features DROP COLUMN IF EXISTS has_discovered_check;

ALTER TABLE game_features DROP COLUMN IF EXISTS has_check;

ALTER TABLE game_features DROP COLUMN IF EXISTS has_checkmate;

ALTER TABLE game_features DROP COLUMN IF EXISTS has_promotion;

ALTER TABLE game_features DROP COLUMN IF EXISTS has_promotion_with_check;

ALTER TABLE game_features DROP COLUMN IF EXISTS has_promotion_with_checkmate;

ALTER TABLE game_features DROP COLUMN IF EXISTS has_back_rank_mate;

ALTER TABLE game_features DROP COLUMN IF EXISTS has_smothered_mate;

ALTER TABLE game_features DROP COLUMN IF EXISTS has_sacrifice;

ALTER TABLE game_features DROP COLUMN IF EXISTS has_zugzwang;

ALTER TABLE game_features DROP COLUMN IF EXISTS has_double_check;

ALTER TABLE game_features DROP COLUMN IF EXISTS has_interference;

ALTER TABLE game_features DROP COLUMN IF EXISTS has_overloaded_piece;
