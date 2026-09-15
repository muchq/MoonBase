# deja

Phase 2 of #1150: a next-request predictor over Caddy's access log that
learns online and shows its work. It tails the live log, turns each
request into one token from caddylog's bounded vocabulary
(`site method route status agent_class[ probe]`), predicts the next token
for that client from a smoothed bigram over the client's last token, scores
the actual request as surprise (`-ln p`), and judges it against an
exponentially weighted baseline: `warmup` for the first 1000 scored
requests, then `expected` or `anomaly` at three deviations, and `novel` for
a token's first sight, which stays out of the baseline. Then it learns the
transition and moves on. Read-only observer: nothing here touches Caddy,
and the only thing written is the checkpoint.

## API

| Route | What |
| --- | --- |
| `GET /deja/v1/stream` | Server-sent events, one per request, `id` the sequence number. 256 seats; a stream ends after ten minutes or when its client falls 64 events behind, and the client reconnects and resumes through `recent`. |
| `GET /deja/v1/recent?after=<seq>` | The last 200 events after `seq`, oldest first. The boundary games_hub will poll (#1554); its raw JSON is pinned by `recent_is_pinned_on_the_wire`. |
| `GET /deja/v1/state` | Sequence, steps, vocabulary size and cap, warmup length, baseline and counts. |
| `GET /health` | server_pal's probe. |

An event carries the lane (a slot number, never an address), the lane's
last eight tokens, the predictor's top five guesses with probabilities, the
actual token, its surprise, the threshold and mean loss it was judged
against (null while warming up), and the verdict. `net` fields are null
until Phase 3.

## Configuration

| Variable | Meaning |
| --- | --- |
| `ACCESS_LOG` | The live log, default `/var/log/caddy/access.log` |
| `DEJA_STATE` | The directory the checkpoint lives in, default `/var/lib/deja`; written every `CHECKPOINT_INTERVAL_SECS` (default 600, at least 1) and on SIGTERM |
| `PORT` | Listen port |

With no checkpoint the current log is learned from its top; with one, only
what is written from then on, so lines written while the process was down
are not learned. A checkpoint that will not parse is moved aside to
`checkpoint.json.corrupt` and the process starts fresh. Client addresses
live only in the lane table in memory, bounded at 4096 clients, and never
leave it.

The vocabulary is capped at 2048 tokens. The factors are bounded but their
product is not small, so a client that walks every route with every method
can fill it; a `vocab_size` at the cap is the signal, and everything after
reads as `<unk>`. Each bigram row keeps its 64 commonest successors.
