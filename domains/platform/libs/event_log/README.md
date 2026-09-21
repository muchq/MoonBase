# event_log

One JSON object per line, on disk, under the names `log_shipper` takes to
S3 — the C++ end of the domain-event path (#1571). games_hub is its first
caller: one line per game that ended.

## Why a writer rather than a log library

The shipper uploads a file and then **deletes** it, so the contract is a
pair of filenames pulling in opposite directions: the file still being
appended to must not match its `rolledLog` pattern, and every file done
with must. Caddy's roller and logback's rolling appender both land on
`<name>-<YYYY-MM-DD>T<HH>.log` with a bare `<name>.log` live beside it,
which is why one pattern reads both.

spdlog's stock sinks do not: `rotating_file_sink` numbers its files and
`daily_file_sink` puts the date on the file it is *currently writing*, so
the shipper would take a live log away mid-hour. Matching the pattern
means a custom sink either way, and at a handful of lines an hour the
rest of a logging framework — levels, formatting, async queues — buys
nothing.

## What it does

`Open(dir, name)` appends to `<dir>/<name>.log`. `Append(when, line)`
writes one line and flushes it; the first write of a new UTC hour first
renames the active file to `<name>-YYYY-MM-DDTHH.log`, stamped with the
hour its lines cover rather than the hour the roll happened in. A restart
takes that hour from the leftover file's last write, so a deploy inside
an hour picks the same file back up and a deploy across one rolls it. A
roll onto a name already taken lands on `<stem>-<n>.log`, which the
shipper matches the same way.

Rolling happens on write, so an idle service rolls late: the last game of
the night waits for the first game of the morning. The events are on disk
throughout — this costs shipping latency, not data — and it is the trade
for having no timer thread.

## What it is not

- **Not application logging.** Stdout under the container's `json-file`
  driver is that, capped and rotated by Docker. This is for bounded
  domain events that something reads back months later.
- **Not a schema.** The caller renders its own line; `event_log` only
  knows that lines end with a newline. games_hub's vocabulary lives in
  `game_events.h`, pinned against the reader in
  `//domains/platform/libs/otel_contract`.
- **Not multi-writer.** One process per directory. Two would interleave
  rolls under one name.
