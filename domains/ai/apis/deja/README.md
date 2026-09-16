# deja

Phase 3 of #1150: a next-request predictor over Caddy's access log that
learns online and shows its work. It tails the live log, turns each
request into one token from caddylog's bounded vocabulary
(`site method route status agent_class[ probe]`), predicts the next token
for that client with two predictors, scores the actual request as
surprise (`-ln p`) under each, and judges it against an exponentially
weighted baseline: `warmup` for the first 1000 scored requests, then
`expected` or `anomaly` at three deviations, and `novel` for a token's
first sight, which stays out of the baseline. Then both predictors learn
the request and move on. Read-only observer: nothing here touches Caddy,
and the only thing written is the checkpoint.

## The two predictors

**bigram**, the control: next-token counts keyed by the client's previous
token, add-α smoothed, 64 successors per row. Cheap, transparent, and the
yardstick the net is measured against.

**net**: a window MLP in candle, CPU, f32. The lane's last eight tokens,
left-padded with `<bos>`, each through a 16-wide embedding, concatenated
(128) → 64 ReLU → the vocabulary cap (2048) → log-softmax; 174,144
parameters. The output layer spans the cap, but the softmax is taken over
the live vocabulary only, so `p` is a distribution over the tokens that
exist rather than one diluted across rows no request has minted — the
same denominator the bigram uses. One AdamW step (lr 0.01) per request,
on that pair plus two drawn uniformly from a replay ring holding the last
512 non-anomalous pairs, oldest evicted first; the step's own forward is
what the event reports, so a request costs one pass, not two. The net
keeps its own baseline over its own surprise on the same terms as the
bigram's.

AdamW's moments are not checkpointed. At 174k parameters and one step per
event — well under a request per second — a restarted net re-warms them
in seconds, which is why the moments are not worth carrying in the
checkpoint's JSON.

**The control decides.** The wire `verdict` and `threshold` are the
bigram's. The net's baseline is reported (`ewma_loss.net`, the
`deja_ewma_loss` gauge) so the learning curve can be watched, and the
question this phase exists to answer — does the net ever beat the
bigram — is answered on the dashboard rather than assumed in the code.
Switching the verdict over is a later decision, made on that evidence.

## API

| Route | What |
| --- | --- |
| `GET /deja/v1/stream` | Server-sent events, one per request, `id` the sequence number. 256 seats; a stream ends after ten minutes or when its client falls 64 events behind, and the client reconnects and resumes through `recent`. |
| `GET /deja/v1/recent?after=<seq>` | The last 200 events after `seq`, oldest first. The boundary games_hub will poll (#1554); its raw JSON is pinned by `recent_is_pinned_on_the_wire`. |
| `GET /deja/v1/state` | Sequence, steps, vocabulary size and cap, warmup length, both baselines, the threshold and counts. |
| `POST /deja/v1/next` | `{"context":["token", ...]}`, one to eight token names → `{"predictions":{"bigram":[...],"net":[...]}}`, each predictor's top five. A name the vocabulary lacks, or a context of the wrong length, is a 400 with `{"error":"..."}` saying which. Asking teaches nothing and makes no event. |
| `GET /health` | server_pal's probe. |

Every route shares one token bucket at 20 requests a second, burst 40.
The limiter keys on the peer address, which behind Caddy is Caddy — so
the page, the hub's two-per-second `recent` poll and anyone asking `next`
draw on the same budget. `next` is the expensive one: a forward pass per
predictor under the engine's lock, run off the async workers so a
question cannot stall the stream or the tailer.

An event carries the lane (a slot number, never an address), the lane's
last eight tokens, each predictor's top five guesses with probabilities,
the actual token, its surprise under each, the threshold and mean loss it
was judged against (threshold null while warming up), and the verdict.
The net never guesses `<bos>`.

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

The checkpoint is one JSON file: sequence, vocabulary, bigram counts,
both baselines, and the net's weights as base64 safetensors under `net`,
so token ids and weights never drift apart and the atomic write covers
both. A checkpoint without `net` (Phase 2's) keeps everything else and
starts the net fresh; so does one whose `net` will not load, with an error
logged. AdamW's moments are not checkpointed: a restarted net resumes
its weights with a cold optimizer, and the replay ring starts empty.

The vocabulary is capped at 2048 tokens, and a checkpoint carrying more
is cut to it on the way in — the net has no row past the cap. The factors
are bounded but their product is not small, so a client that walks every
route with every method can fill it; a `vocab_size` at the cap is the
signal, and everything after reads as `<unk>`. Each bigram row keeps its
64 commonest successors.

## Metrics

`deja_events` by verdict, `deja_surprise` (a running sum) and
`deja_ewma_loss` (a gauge) by predictor, and `deja_vocab_size`, every
series declared at zero. The collector names the counters
`deja_events_total` and `deja_surprise_total`; prom_proxy's `deja` page
reads mean surprise per predictor as the ratio of rates.
