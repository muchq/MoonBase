-- owner_id names the claimant every fence keys on: a heartbeat, a progress
-- write and a terminal write all match on it, and a claim spends an attempt
-- only when the owner it presents differs from the one stored. A blank is a
-- claimant nobody's fence matches and one a re-claim under a blank spends
-- nothing on — so it is refused on both request tables, whoever the writer
-- is. NULL stays what it is: no owner.
--
-- Blanks are nulled on the one execution that adds the constraint, so a
-- database holding one converges rather than failing the deploy. A nulled
-- owner with a lapsed lease is exactly what ClaimNext takes next.

DO $$
DECLARE
    target text;
    constraint_name text;
BEGIN
    FOREACH target IN ARRAY ARRAY['indexing_requests', 'reanalysis_requests']
    LOOP
        constraint_name := target || '_owner_never_blank';
        IF NOT EXISTS (
            SELECT 1 FROM pg_constraint
            WHERE conrelid = target::regclass AND conname = constraint_name
        ) THEN
            EXECUTE format('UPDATE %I SET owner_id = NULL WHERE owner_id = ''''', target);
            EXECUTE format('ALTER TABLE %I ADD CONSTRAINT %I CHECK (owner_id <> '''')',
                           target, constraint_name);
        END IF;
    END LOOP;
END $$;
