-- What TitleRoster falls back to when a refresh cannot read all ten
-- chess.com documents. Without it, a failed refresh writes every player in
-- the month untitled.
--
-- username is stored lowercased and is the key: one row per player, not one
-- per spelling, so the primary key enforces that directly.
--
-- observed_at is when the observation was true, not when it was written, so
-- indexing 2019 after 2026 cannot demote a current GM. A title is never
-- stored empty — a blank row would shadow a real title.

CREATE TABLE IF NOT EXISTS player_titles (
    platform    VARCHAR(50)  NOT NULL,
    username    VARCHAR(255) NOT NULL,
    title       VARCHAR(10)  NOT NULL,
    observed_at TIMESTAMP    NOT NULL,
    source      VARCHAR(20)  NOT NULL,
    CONSTRAINT player_titles_pkey PRIMARY KEY (platform, username)
);
