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
| `GET /deja/v1/stream` | Server-sent events, one per request, `id` the sequence number. Capped at 32 subscribers; a slow one is dropped to resume through `recent`. |
| `GET /deja/v1/recent?after=<seq>` | The last 200 events after `seq`, oldest first. The boundary games_hub polls (#1554); its raw JSON is pinned by `recent_is_pinned_on_the_wire`. |
| `GET /deja/v1/state` | Sequence, steps, vocabulary size, warmup progress, baseline and counts. |
| `GET /health` | server_pal's probe. |

An event carries the lane (a slot number, never an address), the lane's
last eight tokens, the predictor's top five guesses with probabilities, the
actual token, its surprise, the baseline's mean and sigma, the verdict, and
whether the token was novel. `net` fields are null until Phase 3.

## Configuration

| Variable | Meaning |
| --- | --- |
| `ACCESS_LOG` | The live log, default `/var/log/caddy/access.log` |
| `CHECKPOINT` | Learned state, default `/var/lib/deja/checkpoint.json`, written every `CHECKPOINT_INTERVAL_SECS` (default 600) and on SIGTERM |
| `PORT` | Listen port |

With no checkpoint the current log is learned from its top; with one, only
what is written from then on. A checkpoint that will not parse stops the
process rather than silently starting over. Client addresses live only in
the lane table in memory, bounded at 4096 clients, and never leave it.
