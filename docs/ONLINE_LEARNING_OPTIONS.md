# Online Learning Application — Options

Design notes for [#1150](https://github.com/muchq/MoonBase/issues/1150): a fun online-learning
application that runs unsupervised on the consolidated server.

## Constraints

- **Host budget**: 2 vCPU / 2 GB RAM / 60 GB SSD, no GPU. The compose file's memory limits are
  already oversubscribed (~4.8 GB of limits on a 2 GB box), so a new service realistically gets
  **≤ 0.5 CPU and 256–512 MB** and must degrade gracefully under contention.
- **Unsupervised**: no babysitting. The service must survive restarts (persist state), resist
  garbage/adversarial input, and never need a human to un-wedge it.
- **Online learning**: the model improves from a live stream of interactions, not offline batch jobs
  someone has to run.
- **Tech**: Rust w/candle, C++, Java, or Go. No Python, no PyTorch.

## Assets already in the repo

| Asset | Where | Why it matters |
|---|---|---|
| microgpt (train + infer + serve) | `domains/ai/libs/microgpt`, `apis/microgpt_serve` | CPU-friendly tiny GPT with checkpoints, safetensors, chat encoding — already deployed at tty1.uk |
| neuro (Go NN lib) | `domains/ai/libs/neuro` | Dense/Dropout/BatchNorm, SGD/Adam, pure Go — ideal for small online-SGD demos |
| games_ws_backend | `domains/games/apis/games_ws_backend` | WebSocket multiplayer plumbing for live, spectate-able experiences |
| Observability stack | `deploy/consolidated` | Prometheus + Grafana: loss curves, accuracy, and "is it learning?" dashboards for free |
| Caddy + compose | `deploy/consolidated` | One more container is a 10-line diff |

---

## Option 1 — RPS Mind Reader (online sequence prediction)

**Concept**: Rock-paper-scissors against a model that learns *your* patterns as you play. Humans are
terrible at being random; after ~10 rounds a decent predictor wins noticeably more than 33%. Show
the model's live prediction confidence *after* each round ("I was 71% sure you'd throw rock — here's
why") and a running scoreboard of human vs. machine.

- **Learning**: a mixture of online experts (per-order n-gram / Markov predictors over your move and
  outcome history, combined with multiplicative-weights or a small online logistic layer). Two tiers:
  a per-session model that learns you in seconds, and a global model that slowly learns humanity.
  Every round is a true online update — no batches, no training jobs.
- **Tech**: Go (fits `games_ws_backend` patterns) or **straight C++** — the model is a few KB of
  counters and weights; C++ with a tiny HTTP/WS layer is a perfectly honest fit, no tensor lib needed.
- **Resources**: negligible. <0.1 CPU, <64 MB. State snapshot is a small file.
- **Understandable**: maximally. You can render the entire model on screen (per-expert weights,
  what each expert predicted).
- **Unsupervised risks**: essentially none — worst case an adversary plays randomly and the model
  correctly learns to be 33% against them.
- **Effort**: small (a weekend). Good candidate to ship first and establish the "learning in public"
  dashboard pattern.

## Option 2 — Doodle classifier that learns from visitors (neuro)

**Concept**: Quick, Draw!-style. Visitor sketches a digit/shape on a canvas, the model guesses live
(top-3 with confidences), the visitor confirms or corrects, and the model takes an SGD step *right
then*. A public dashboard shows accuracy-over-time, per-class confusion, and first-layer weight
visualizations evolving day by day.

- **Learning**: online SGD (optionally with a small replay buffer of past strokes to smooth updates).
  This is the cleanest "general online learning for neural networks" demo: labels come free from the
  interaction loop.
- **Tech**: Go with **neuro** — this is exactly what the library was built for. Canvas frontend is
  vanilla JS like `1d4_web`.
- **Resources**: a 28×28-input MLP is tiny; <0.25 CPU, ~128 MB including replay buffer.
- **Unsupervised risks**: label trolling (drawing a cat and labeling it "7"). Mitigations: per-IP
  trust weighting, only apply updates the model doesn't find wildly inconsistent (loss-gated updates),
  and a frozen holdout canary set whose accuracy is graphed — if canary accuracy drops, auto-revert to
  the last good checkpoint. That auto-revert loop is what makes it genuinely unsupervised.
- **Effort**: medium. The fun-per-watt is high and it exercises neuro end to end.

## Option 3 — kNN-LM memory for microgpt (memory experiments, no retraining)

**Concept**: Give microgpt a *non-parametric memory*. Every conversation appends
(hidden state → next token) pairs to a datastore. At inference, interpolate the model's logits with
a nearest-neighbor lookup over that datastore (kNN-LM, Khandelwal et al.). The model's weights never
change, yet it visibly "remembers" things visitors said — tell it a fact, come back later, watch it
surface. A UI toggle for memory on/off (and a λ slider for interpolation strength) makes the effect
legible to visitors.

- **Learning**: online in the purest sense — the datastore grows with every interaction, zero
  gradient steps, zero catastrophic forgetting, instant "learning".
- **Tech**: Rust/candle, extending `microgpt_serve`. Exact brute-force kNN is fine at this scale:
  with `n_embd = 256`, 1M entries at f16 is ~512 MB — so cap the store (e.g. 200k entries ≈ 100 MB
  with FIFO or importance-based eviction, which is itself an interesting knob to expose).
- **Resources**: 0.5 CPU / 512 MB — same envelope as microgpt-serve today; could even live inside it.
- **Understandable**: shows *retrieved neighbors* next to each generated token — "the model said
  'purple' because these 3 past conversations did."
- **Unsupervised risks**: memory poisoning (visitors teaching it garbage). Mitigations: per-source
  quotas, profanity filter on ingest, eviction favors entries that actually get retrieved and improve
  next-token agreement. Being non-parametric means a bad entry is *deletable* — unlike a bad gradient step.
- **Effort**: medium. This is the strongest match for "interesting memory experiments with llms
  (microgpt adjacent)" and it's genuinely novel territory for a hobby server.

## Option 4 — microgpt that grows up in public (continual training)

**Concept**: A tiny chat model (1–8M params) that periodically retrains on the conversations it has.
The site's identity is the narrative: "this model was born on day 0 knowing nothing; here's its diary."
Public Grafana panels show loss, canary-prompt answers over time, and vocabulary growth.

- **Learning**: continual/incremental fine-tuning — a low-priority CPU training loop (nightly or
  drip-fed steps between requests) over a mixed batch: new conversations + replay of the original
  corpus (experience replay ratio is the key anti-forgetting knob; EWC is a stretch experiment).
- **Tech**: Rust/candle — `tensor_train.rs` already supports resume-from-checkpoint with optimizer
  state, which is 80% of the plumbing.
- **Resources**: this is the tight one. Training even a 2M-param model on 2 shared vCPUs is slow;
  it must run under a hard cgroup CPU cap (e.g. 0.5 CPU) and accept few steps/day. RAM for
  training (weights + Adam moments + activations) at 2M params is fine (<300 MB), at 8M it's pushing it.
- **Unsupervised risks**: highest of all options — data poisoning, catastrophic forgetting, and the
  model degrading into repeating whatever visitors type. Mitigations: replay buffer, canary eval set
  with auto-revert to last good checkpoint, ingest filters, rate limits per IP. Doable, but this is
  the option most likely to need occasional human attention, which cuts against "unsupervised."
- **Effort**: medium-high. Natural *phase 2* after Option 3: the kNN memory becomes the staging area,
  and only high-quality remembered data graduates into training batches.

## Option 5 — Neuroevolution aquarium (straight C++)

**Concept**: A persistent 2D world — an aquarium — of creatures whose brains are tiny neural nets
(fixed-topology MLPs or NEAT), evolving continuously: eat, avoid hazards, reproduce with mutation.
Visitors spectate a live stream of the world, drop food or hazards, name creatures, and click any
creature to see its brain (weights, sensor activations) live. Generation count and fitness curves on
the public dashboard. It runs forever whether or not anyone is watching.

- **Learning**: evolutionary rather than gradient-based — still honest online learning, arguably the
  most *visible* kind: behavior observably improves over days.
- **Tech**: **straight C++** and this is the good use case for it — a fixed-timestep simulation loop
  with thousands of tiny matmul-free brains is exactly what C++ is for; no tensor library, no GC
  jitter, deterministic replay from a seed. Serve state over WebSocket (either a small C++ WS server
  or fronted by the existing Go `games_ws_backend` pattern); render client-side on canvas so the
  server ships only entity state.
- **Resources**: tunable to whatever budget exists — tick rate and population size are dials.
  0.25 CPU / 128 MB runs a respectable ecosystem. Snapshot world + genomes to disk periodically.
- **Unsupervised risks**: low — worst case is ecosystem collapse, which is handled by auto-reseeding
  from the hall-of-fame genome archive (and "the great extinction of August 2026" is itself content).
- **Effort**: medium. Highest "leave it running for a year and it's still interesting" score.

## Option 6 — Graph RAG over chess data (stretch)

**Concept**: Incrementally build a knowledge graph from `one_d4`'s indexed chess games (players,
openings, motifs, results) as games are indexed, and answer questions by walking the graph.

- **Why it's ranked last**: the graph-building is fun and cheap (Go or Java, Postgres is already
  there), but the "RAG" half wants a language model that can actually read retrieved context —
  microgpt at 1–8M params can't. A quantized Qwen-0.5B via candle *fits* in ~500 MB but would be
  painfully slow on 2 shared vCPUs. The honest version is "graph queries with template answers,"
  which is a query engine (ChessQL already exists), not online learning.
- **Verdict**: park it, or revisit as a memory-graph layer on top of Option 3 (entities extracted
  from conversations forming a graph memory — that *would* be a real graph-RAG-meets-memory experiment).

---

## Comparison

| Option | Tech | CPU / RAM | Online-learning type | Unsupervised risk | Effort | Fun/watt |
|---|---|---|---|---|---|---|
| 1. RPS Mind Reader | Go or C++ | ~0.1 / 64 MB | online experts, per-interaction | none | S | high |
| 2. Doodle classifier | Go (neuro) | 0.25 / 128 MB | online SGD w/ replay | label trolling (auto-revert) | M | high |
| 3. kNN-LM memory | Rust/candle | 0.5 / 512 MB | non-parametric, instant | memory poisoning (deletable) | M | high |
| 4. Continual microgpt | Rust/candle | 0.5 capped / 300 MB | continual fine-tuning | forgetting + poisoning | M-L | medium |
| 5. Neuroevolution aquarium | C++ | 0.25 / 128 MB | evolutionary | ecosystem collapse (reseed) | M | high |
| 6. Chess graph RAG | Go/Java | n/a | weak fit | n/a | L | low |

## Recommendation

- **Ship first**: **Option 1 (RPS Mind Reader)** — a weekend of work, zero moderation surface, and it
  establishes the pattern every other option reuses: a model whose internals are rendered live, with
  Prometheus metrics proving it's learning.
- **Main event**: **Option 3 (kNN-LM memory for microgpt)** — best match for the "memory experiments,
  microgpt adjacent" interest, genuinely novel, safe to run unsupervised because memory is inspectable
  and deletable, and it sets up Option 4 (continual training) as a later phase with the kNN store as
  the curated data source.
- **If the itch is C++**: **Option 5 (aquarium)** — the strongest straight-C++ use case in the list,
  and the best "alive forever without supervision" property.

All three coexist within the leftover resource envelope if scheduled sensibly (the aquarium and RPS
are nearly free; kNN-LM replaces/extends the existing microgpt-serve budget).
