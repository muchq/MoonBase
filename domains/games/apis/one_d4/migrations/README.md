# one_d4 schema migrations

The schema as numbered, idempotent SQL files (#1419). This directory is the
one copy of the DDL: the `one_d4_migrate` deploy step applies it before the
services start, the Java service verifies at boot that it did, and
`one_d4_worker`'s Postgres suites apply it to build the schema they test
against.

## Layout

- `manifest.txt` — the ordering. A step not listed here never runs.
- `V<NNN>__<name>.sql` — a step whose SQL both engines share.
- `pg/V<NNN>__<name>.sql`, `h2/V<NNN>__<name>.sql` — a step where the
  engines fork. A forked step has a file in *both* engine directories and
  none at the top level; a shared step has only the top-level file. Anything
  else fails `MigrationFilesTest` and `Migration` itself refuses to run it.

Postgres is the deployment engine. H2 is the test engine — `h2/` files ship
only on test classpaths (`:test_db`), never in the service image, and exist
so the default CI suite exercises the same migration path (see the dedupe
notes in `V009__dedupe_key.sql` for why that is load-bearing).

## Rules

- **Idempotent, always.** Every statement must be safe to re-run:
  `IF NOT EXISTS`, `IF EXISTS`, `DO $$ ... EXCEPTION` blocks, guarded
  UPDATEs. There is no tracking table; re-running everything *is* the
  mechanism, and it is what lets the deploy step run on every deploy. What
  would end that: boot/deploy time growing with the step count, or a step
  that cannot be written idempotently (a backfill too expensive to guard).
  Neither exists yet.

  Idempotence is also what `Migration.verify()` leans on — it applies the
  steps to an empty scratch schema and compares, so a step that only works
  against a populated database breaks boot verification, not just re-runs.
- **Append, don't edit.** A schema change is a new `V<NNN>` step: the next
  number, a line in `manifest.txt`, and the file named in *each* applicable
  `BUILD.bazel` list — `:migrations` (pg + shared, ships with the service),
  `:h2_migrations` (h2, test-only), `:migrations_sql` (all of them, what
  the C++ suites run and walk). A file unlisted in BUILD neither ships nor
  runs, and no test can see it. Editing an old step is for comments only.
- **Plain SQL, and a whole file has to work as one script.** Each `pg/` and
  shared file also works under `psql -f`; nothing here depends on the runner
  (the `h2/` files are H2 syntax and are not psql-compatible). Java splits
  on top-level semicolons — dollar-quoting, `''` escapes and comments
  respected — and executes them individually, so a failure names its step;
  the splitter does not model double-quoted identifiers or `E''` strings, so
  don't use them (none of the schema needs either). `one_d4_worker`'s
  Postgres suites send each `pg/` or shared file whole through libpq
  instead (`migration_files`), which puts its statements in one implicit
  transaction — so no step may depend on an earlier statement in the same
  file having committed.
- **Forked steps stay in step.** Both engines run the same step list in the
  same order; only a step's SQL may differ. If you add a partial index on
  Postgres, add the H2 stand-in with the same name (see `V016`, `V017`).

## Running them by hand

```bash
for step in $(grep -v '^#' manifest.txt); do
  f="pg/$step.sql"; [ -f "$f" ] || f="$step.sql"
  psql "$DATABASE_URL" -v ON_ERROR_STOP=1 -f "$f"
done
```

The deploy step (`//domains/games/apis/one_d4:one_d4_migrate`, a one-shot
compose service) does the same through the Java `Migration` class, so the
statements production runs are the ones the tests ran. Both `one_d4` and
`one_d4_worker` gate on it — the service because its boot check would
otherwise run against a schema the one-shot has not finished writing, the
worker because it can then start without the Java service at all.
