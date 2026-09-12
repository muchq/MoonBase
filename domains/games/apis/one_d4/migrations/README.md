# one_d4 schema migrations

The schema as numbered, idempotent SQL files (#1419). This directory is the
one copy of the DDL: the `one_d4_migrate` deploy step applies it before the
services start, the Java service verifies at boot that it did, and both the
Java and `one_d4_worker` suites apply it to build the schema they test
against.

## Layout

- `manifest.txt` — the ordering. A step not listed here never runs.
- `V<NNN>__<name>.sql` — a step. One file, one engine: Postgres, the engine
  every suite and the deployment run (#1532).

`V001__initial_schema.sql` is the whole schema today. It was collapsed from
eighteen steps in #1532, which is a thing you can do exactly once and only
while no deployed database has state worth keeping — there is no tracking
table, so the files are re-executed in full every deploy and nothing records
which of them a given database has seen. From here the rule below applies.

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
  number, a line in `manifest.txt`, and the file named in
  `:migrations_sql_files` (`BUILD.bazel`) — the one list both `:migrations`
  and `:migrations_sql` compose.

  Editing an applied step is for comments only, and the trap is specific:
  a step that *adds* something a later step *drops* runs both halves on
  every deploy, forever. Postgres never reuses a dropped column's attnum
  and counts it against the 1600-column ceiling, so an add/drop pair spends
  one permanently per deploy until `ADD COLUMN` starts failing. If a step's
  effect is meant to go away, the step that added it is what should stop
  adding it. A file unlisted in BUILD neither ships nor runs, and no
  test can see it. Editing an old step is for comments only.
- **Plain SQL, and a whole file has to work as one script.** Every file also
  works under `psql -f`; nothing here depends on the runner. Java splits on
  top-level semicolons — dollar-quoting, `''` escapes and comments respected
  — and executes them individually, so a failure names its step; the splitter
  does not model double-quoted identifiers or `E''` strings, so don't use
  them (none of the schema needs either). `one_d4_worker`'s Postgres suites
  send each file whole through libpq instead (`migration_files`), which puts
  its statements in one implicit transaction — so no step may depend on an
  earlier statement in the same file having committed.

## Running them by hand

```bash
for step in $(grep -v '^#' manifest.txt); do
  psql "$DATABASE_URL" -v ON_ERROR_STOP=1 -f "$step.sql"
done
```

The deploy step (`//domains/games/apis/one_d4:one_d4_migrate`, a one-shot
compose service) does the same through the Java `Migration` class, so the
statements production runs are the ones the tests ran. Both `one_d4` and
`one_d4_worker` gate on it — the service because its boot check would
otherwise run against a schema the one-shot has not finished writing, the
worker because it can then start without the Java service at all.
