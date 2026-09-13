-- The stored spelling of platform is canonical: trimmed, upper-cased, dots
-- to underscores. CHESS_COM and LICHESS, never chess.com.
--
-- ChessQL canonicalises a platform literal before binding it (#1539), so a
-- row stored any other way is unreachable by every spelling a user can type
-- — zero rows from a query that reads correctly, which is the symptom that
-- bug was filed for. Until now the premise was enforced only by every writer
-- happening to agree: four bare VARCHARs and a rule living in Java.
--
-- Stated as "nothing canonical() would change" rather than as canonical()
-- rewritten in SQL. Replicating it needs the same trim set on both sides, and
-- btrim with no argument takes spaces where Java's strip() also takes tabs and
-- newlines — so a tab-padded value satisfied the rule while ChessQL bound the
-- trimmed form, which is the unreachable row this exists to prevent.
--
-- The three clauses are the three things canonical() does. Where the whitespace
-- classes still disagree at the edges, they disagree toward refusing a write
-- rather than accepting one nothing can read: a failed insert says so.
--
-- Empty is not canonical here even though canonical('') is '': what the column
-- has to hold is what the request path stores, and that refuses a blank.
-- Interior whitespace is left alone, because canonical() leaves it alone.
--
-- The normalise runs only on the deploy that adds the constraint, not on
-- every deploy — a guarded UPDATE would seq-scan game_features forever. A
-- database holding a spelling that collides on a unique key fails here rather
-- than converging, which is the right answer for one somebody edited by hand.

DO $$
DECLARE
    target text;
    canonical constant text := 'replace(upper(btrim(platform)), ''.'', ''_'')';
    is_canonical constant text :=
        'platform = upper(platform)'
        || ' AND position(''.'' in platform) = 0'
        || ' AND platform !~ ''^[[:space:]]'''
        || ' AND platform !~ ''[[:space:]]$'''
        || ' AND platform <> ''''';
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
            EXECUTE format('ALTER TABLE %I ADD CONSTRAINT %I CHECK (%s)', target,
                           constraint_name, is_canonical);
        END IF;
    END LOOP;
END $$;
