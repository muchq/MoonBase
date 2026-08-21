# one_d4 schema migrations

The schema as numbered, idempotent SQL files (#1419). This directory is the
one copy of the DDL: the Java service applies it at boot, the
`one_d4_migrate` deploy step applies it before the services start, and
`one_d4_worker`'s `schema_contract_test` reads it to hold the C++ fixtures
to it.

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
  mechanism, and the Java service re-runs it on every boot (#1426 tracks
  demoting that boot-time run to a verifier — and it is where the tracking
  table question reopens, if boot time ever grows with the step count or a
  step arrives that cannot be written idempotently).
- **Append, don't edit.** A schema change is a new `V<NNN>` step: the next
  number, a line in `manifest.txt`, and the file named in *each* applicable
  `BUILD.bazel` list — `:migrations` (pg + shared, ships with the service),
  `:h2_migrations` (h2, test-only), `:migrations_sql` (all of them, the C++
  contract test's data). A file unlisted in BUILD neither ships nor runs,
  and no test can see it. Editing an old step is for comments only.
- **Plain SQL, executed one statement at a time.** Each `pg/` and shared
  file also works under `psql -f`; nothing here depends on the runner (the
  `h2/` files are H2 syntax and are not psql-compatible). Statements are
  split on top-level semicolons — dollar-quoting, `''` escapes and comments
  respected — and executed individually, exactly as the old Java constants
  were: whole-file execute would trade that for the driver's own
  multi-statement semantics and lose which step failed. The splitter does
  not model double-quoted identifiers or `E''` strings, so don't use them
  (none of the schema needs either).
- **Forked steps stay in step.** Both engines run the same step list in the
  same order; only a step's SQL may differ. If you add a partial index on
  Postgres, add the H2 stand-in with the same name (see `V016`, `V017`).
- **The C++ fixtures are held to `pg/` + shared.**
  `schema_contract_test` scrapes `CREATE TABLE` bodies and
  `ALTER TABLE ... ADD COLUMN IF NOT EXISTS` lines, so keep one column per
  line in CREATE bodies and each ALTER on a single line.

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
`one_d4_worker` gate on it — the service so the two migration runners are
serialized rather than concurrent, the worker so it can start without the
Java service at all.
