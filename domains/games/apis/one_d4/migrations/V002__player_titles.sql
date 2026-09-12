-- Titles that outlive the worker process.
--
-- TitleRoster answers TitleOf() from ten chess.com documents held in
-- memory, so a failed refresh leaves it with nothing and every player in
-- the month indexed under it is written untitled. This is what it falls
-- back to: the last roster it managed to read, which is a day stale at
-- worst and right about everyone who did not change title today.
--
-- username is stored lowercased and is the key. Uniqueness here is
-- semantic — one row per player, not one per spelling — so folding on the
-- way in makes the primary key enforce it directly, rather than a UNIQUE
-- index on LOWER(username) that every reader then has to remember to match.
-- game_features keeps the casing a player typed; nothing displays a row
-- from this table.
--
-- observed_at is when the observation was true, not when it was written:
-- a roster read stamps now, and a title read out of an old game's PGN
-- headers stamps that game. Writers compare it so that indexing 2019
-- after 2026 cannot demote a current GM, and a title is never stored
-- empty — absence of a title header is every untitled player too, so a
-- blank row would shadow a real title rather than record anything.

CREATE TABLE IF NOT EXISTS player_titles (
    platform    VARCHAR(50)  NOT NULL,
    username    VARCHAR(255) NOT NULL,
    title       VARCHAR(10)  NOT NULL,
    observed_at TIMESTAMP    NOT NULL,
    source      VARCHAR(20)  NOT NULL,
    CONSTRAINT player_titles_pkey PRIMARY KEY (platform, username)
);
