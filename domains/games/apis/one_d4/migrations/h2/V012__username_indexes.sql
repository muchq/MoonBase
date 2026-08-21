-- The same names on H2, which has no expression indexes — and a plain
-- column index cannot serve a LOWER(...) predicate there for exactly the
-- reason pg/V012 gives, so on H2 these are pure write cost, not a weaker
-- plan. They are carried anyway so the migration path stays identical on
-- both engines and the H2 suite can pin their existence; H2 is the test
-- engine, so the cost lands on tests, not production.

CREATE INDEX IF NOT EXISTS idx_game_features_white_username ON game_features(white_username);

CREATE INDEX IF NOT EXISTS idx_game_features_black_username ON game_features(black_username);
