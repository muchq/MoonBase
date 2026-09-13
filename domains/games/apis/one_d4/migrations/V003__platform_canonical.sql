-- The stored spelling of platform is canonical: trimmed, upper-cased, dots
-- to underscores. CHESS_COM and LICHESS, never chess.com.
--
-- ChessQL canonicalises a platform literal before binding it (#1539), so a
-- row stored any other way is unreachable by every spelling a user can type
-- — zero rows from a query that reads correctly, which is the symptom that
-- bug was filed for. Until now the premise was enforced only by every writer
-- happening to agree: four bare VARCHARs and a rule living in Java.
--
-- The expression is Platforms.canonical, in SQL. PlatformCanonicalConstraintTest
-- stores what that method returns, so the two spellings of one rule cannot
-- drift into rejecting each other.
--
-- Empty is not canonical here even though canonical('') is '': what the column
-- has to hold is what the request path stores, and that refuses a blank.
--
-- The normalise runs only on the deploy that adds the constraint, not on
-- every deploy — a guarded UPDATE would seq-scan game_features forever. A
-- database holding a spelling that collides on a unique key fails here rather
-- than converging, which is the right answer for one somebody edited by hand.

DO $$
DECLARE
    target text;
    canonical constant text := 'replace(upper(btrim(platform)), ''.'', ''_'')';
    constraint_name text;
BEGIN
    FOREACH target IN ARRAY ARRAY[
        'indexing_requests', 'game_features', 'indexed_periods', 'player_titles'
    ]
    LOOP
        constraint_name := target || '_platform_canonical';
        IF NOT EXISTS (
            SELECT 1 FROM pg_constraint
            WHERE conrelid = target::regclass AND conname = constraint_name
        ) THEN
            EXECUTE format(
                'UPDATE %I SET platform = %s WHERE platform <> %s',
                target, canonical, canonical);
            EXECUTE format(
                'ALTER TABLE %I ADD CONSTRAINT %I CHECK (platform = %s AND platform <> '''')',
                target, constraint_name, canonical);
        END IF;
    END LOOP;
END $$;
