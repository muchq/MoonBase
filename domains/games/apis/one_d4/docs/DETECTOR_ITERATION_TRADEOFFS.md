# Trade-offs: Detector-Centric vs Position-Centric Iteration

This document analyzes two ways to run motif detection over a game’s positions:

1. **Current (detector-centric):** The orchestrator calls each detector once with the full list of positions; each detector iterates through every position (or every pair) itself.
2. **Alternative (position-centric):** The orchestrator iterates over positions (or position pairs) and invokes each detector once per position (or per pair).

We compare them for cache locality, memory use, API design, and parallelism, and include diagrams with a consistent color scheme.

---

## Current Approach: Detector-Centric

The **FeatureExtractor** loops over detectors. Each detector receives the entire `List<PositionContext>` and performs its own loop over positions (or over consecutive pairs).

```mermaid
flowchart TB
  subgraph extract["FeatureExtractor.extract()"]
    PGN[PGN]
    PGN --> Replay[GameReplayer.replay]
    Replay --> Positions["List&lt;PositionContext&gt;<br/>positions 1..M"]
  end

  subgraph loop["Outer loop: for each detector"]
    Positions --> D1[PinDetector.detect positions]
    Positions --> D2[SkewerDetector.detect positions]
    Positions --> D3[CheckDetector.detect positions]
    Positions --> D4[AttackDetector.detect positions]
    Positions --> D5["... other detectors"]
  end

  subgraph inner["Each detector iterates internally"]
    D1 --> I1["for (ctx : positions)"]
    D2 --> I2["for (ctx : positions)"]
    D3 --> I3["for (ctx : positions)"]
    D4 --> I4["for (i=1..M) before=pos[i-1], after=pos[i]"]
    D5 --> I5["for (...)"]
  end

  I1 --> Out[allOccurrences]
  I2 --> Out
  I3 --> Out
  I4 --> Out
  I5 --> Out

  style Positions fill:#E3F2FD,stroke:#1565C0
  style D1 fill:#FFE0B2,stroke:#E65100
  style D2 fill:#FFE0B2,stroke:#E65100
  style D3 fill:#FFE0B2,stroke:#E65100
  style D4 fill:#FFE0B2,stroke:#E65100
  style D5 fill:#FFE0B2,stroke:#E65100
  style I1 fill:#C8E6C9,stroke:#2E7D32
  style I2 fill:#C8E6C9,stroke:#2E7D32
  style I3 fill:#C8E6C9,stroke:#2E7D32
  style I4 fill:#C8E6C9,stroke:#2E7D32
  style Out fill:#E1BEE7,stroke:#7B1FA2
```

**Access pattern:** Detector 1 touches position 1, 2, …, M; then detector 2 touches 1, 2, …, M; and so on. So the same positions are revisited once per detector.

---

## Alternative Approach: Position-Centric

The orchestrator loops over **positions** (or over **index** for pair-based logic). For each step, it invokes every detector that cares about that step, passing a single position or a (before, after) pair.

```mermaid
flowchart TB
  subgraph extract["FeatureExtractor.extract()"]
    PGN[PGN]
    PGN --> Replay[GameReplayer.replay]
    Replay --> Positions["List&lt;PositionContext&gt;<br/>positions 1..M"]
  end

  subgraph loop["Outer loop: for each position (or pair)"]
    Positions --> Step["Step i: position i<br/>or pair (i-1, i)"]
  end

  subgraph invoke["For this step, invoke detectors once"]
    Step --> D1[PinDetector.detectAt i]
    Step --> D2[SkewerDetector.detectAt i]
    Step --> D3[CheckDetector.detectAt i]
    Step --> Pair["Step i: pair (i-1,i)"]
    Pair --> D4[AttackDetector.detectPair i-1, i]
  end

  D1 --> Out[allOccurrences]
  D2 --> Out
  D3 --> Out
  D4 --> Out

  style Positions fill:#E3F2FD,stroke:#1565C0
  style Step fill:#BBDEFB,stroke:#0D47A1
  style Pair fill:#BBDEFB,stroke:#0D47A1
  style D1 fill:#FFE0B2,stroke:#E65100
  style D2 fill:#FFE0B2,stroke:#E65100
  style D3 fill:#FFE0B2,stroke:#E65100
  style D4 fill:#FFE0B2,stroke:#E65100
  style Out fill:#E1BEE7,stroke:#7B1FA2
```

**Access pattern:** For position i we load position i (and i−1 when needed) once and run all single-position detectors; for pair (i−1, i) we run pair-based detectors. Each position’s data is touched only when that position (or pair) is the current step.

---

## Side-by-Side Data Flow

```mermaid
flowchart LR
  subgraph current["Detector-centric"]
    direction TB
    A1[positions] --> B1[D1: scan 1..M]
    A1 --> B2[D2: scan 1..M]
    A1 --> B3[D3: scan 1..M]
    A1 --> B4[D4: scan pairs 1..M]
  end

  subgraph alt["Position-centric"]
    direction TB
    C1[positions] --> D1["Step 1: D1,D2,D3 on pos1"]
    C1 --> D2["Step 2: D1,D2,D3 on pos2; D4 on pair 1-2"]
    C1 --> D3["Step 3: D1,D2,D3 on pos3; D4 on pair 2-3"]
    C1 --> D4["... Step M"]
  end

  style A1 fill:#E3F2FD,stroke:#1565C0
  style C1 fill:#E3F2FD,stroke:#1565C0
  style B1 fill:#FFCCBC,stroke:#BF360C
  style B2 fill:#FFCCBC,stroke:#BF360C
  style B3 fill:#FFCCBC,stroke:#BF360C
  style B4 fill:#FFCCBC,stroke:#BF360C
  style D1 fill:#C5CAE9,stroke:#3949AB
  style D2 fill:#C5CAE9,stroke:#3949AB
  style D3 fill:#C5CAE9,stroke:#3949AB
  style D4 fill:#C5CAE9,stroke:#3949AB
```

---

## Trade-offs Summary

| Dimension | Detector-centric (current) | Position-centric (alternative) |
|-----------|----------------------------|----------------------------------|
| **Cache locality** | Positions are re-scanned per detector: position 1, then 2, … then again from 1 for the next detector. More cache misses when the position list doesn’t fit in cache. | Each position (or pair) is loaded once per “step”; all detectors that need that step run before moving on. Better locality for position data. |
| **Memory** | One shared list; each detector only holds references and local state. Low and simple. | Same list; orchestrator may need to pass (before, after) or (list, index). Similar memory use. |
| **API** | Simple: `detect(List<PositionContext> positions)`. Detector owns its iteration and can optimize (e.g. skip last N, or only last position for mate). | Needs a different contract: e.g. `detectAt(List<PositionContext> positions, int index)` for single-position, and `detectPair(positions, index)` or `detect(before, after)` for pair-based. More variants or a single “step” type. |
| **Pair-based detectors** | AttackDetector and DiscoveredAttackDetector naturally loop over `i = 1..size-1` and use `positions.get(i-1)`, `positions.get(i)`. No API change. | Orchestrator must drive “steps” for pairs (e.g. for i in 1..M-1 call AttackDetector with (positions.get(i-1), positions.get(i))). Same work, different owner. |
| **Parallelism** | Easy to parallelize **over detectors**: run each detector on the full list in parallel (each thread gets one detector). No shared mutable state between detectors. | Easy to parallelize **over positions**: each thread gets a range of indices and runs all detectors for those steps. Fewer positions per thread can mean better locality; need to merge occurrence lists. |
| **Code clarity** | Each detector’s logic (including “which positions I care about”) lives in one place. FeatureExtractor stays trivial. | Orchestrator must know which detectors need single position vs pair and call them in the right way. More logic in the orchestrator; detectors become simpler per-call but the “schedule” is centralized. |
| **Short-circuit / early exit** | Detectors can skip positions (e.g. mate detectors that only look at the last position). No change. | Orchestrator can avoid calling a detector for steps it doesn’t care about (e.g. call BackRankMate only for the last step). Requires orchestrator to encode that knowledge. |

---

## Detector Categories

Not all detectors use positions the same way. The trade-offs depend on that.

```mermaid
flowchart TB
  subgraph single["Single-position detectors (one ctx per call)"]
    S1[PinDetector]
    S2[SkewerDetector]
    S3[CheckDetector]
    S4[PromotionDetector]
    S5[PromotionWithCheckDetector]
    S6[CrossPinDetector]
    S7[BackRankMateDetector]
    S8[SmotheredMateDetector]
    S9[PromotionWithCheckmateDetector]
  end

  subgraph pair["Pair detectors (before + after)"]
    P1[AttackDetector]
    P2[DiscoveredAttackDetector]
  end

  style S1 fill:#A5D6A7,stroke:#2E7D32
  style S2 fill:#A5D6A7,stroke:#2E7D32
  style S3 fill:#A5D6A7,stroke:#2E7D32
  style S4 fill:#A5D6A7,stroke:#2E7D32
  style S5 fill:#A5D6A7,stroke:#2E7D32
  style S6 fill:#A5D6A7,stroke:#2E7D32
  style S7 fill:#A5D6A7,stroke:#2E7D32
  style S8 fill:#A5D6A7,stroke:#2E7D32
  style S9 fill:#A5D6A7,stroke:#2E7D32
  style P1 fill:#90CAF9,stroke:#1565C0
  style P2 fill:#90CAF9,stroke:#1565C0
```

- **Single-position:** In a position-centric design, the orchestrator would call each of these once per position (or, for mate detectors, only for the last position if we encode that).
- **Pair:** The orchestrator would call once per consecutive pair (e.g. indices 1..M−1 with `(positions.get(i-1), positions.get(i))`).

---

## Horizontal Scaling of Index Workers

When we **scale out** by running many index workers (e.g. multiple processes, containers, or nodes), each worker typically:

- Pulls work from a shared **queue** (index requests or per-game tasks).
- Processes one or more games at a time (replay → extract → insert).
- Shares no mutable state with other workers; each game is independent.

Under that model, the choice between detector-centric and position-centric iteration affects **per-worker behavior**, **memory footprint**, and **how we use multiple cores inside a worker**. This section analyzes those effects.

### Scaling model

```mermaid
flowchart TB
  subgraph queue["Work queue"]
    Q[Index requests or game PGNs]
  end

  subgraph workers["Index workers (horizontal scale)"]
    W1[Worker 1]
    W2[Worker 2]
    W3[Worker 3]
    W4["... Worker N"]
  end

  Q --> W1
  Q --> W2
  Q --> W3
  Q --> W4

  W1 --> G1["Game A: replay → extract → insert"]
  W2 --> G2["Game B: replay → extract → insert"]
  W3 --> G3["Game C: replay → extract → insert"]
  W4 --> G4["..."]

  G1 --> DB[(DB)]
  G2 --> DB
  G3 --> DB
  G4 --> DB

  style Q fill:#FFF9C4,stroke:#F9A825
  style W1 fill:#BBDEFB,stroke:#1565C0
  style W2 fill:#BBDEFB,stroke:#1565C0
  style W3 fill:#BBDEFB,stroke:#1565C0
  style W4 fill:#BBDEFB,stroke:#1565C0
  style G1 fill:#C8E6C9,stroke:#2E7D32
  style G2 fill:#C8E6C9,stroke:#2E7D32
  style G3 fill:#C8E6C9,stroke:#2E7D32
  style DB fill:#E1BEE7,stroke:#7B1FA2
```

- **Adding workers** increases total throughput (more games per second) as long as the queue and downstream (DB, API) can keep up. Neither iteration style blocks horizontal scaling: workers remain stateless and independent.
- The **iteration style** matters for (a) how many games one worker can process in parallel (memory and locality), and (b) how we use multiple cores **within** one worker for a single game.

### Per-worker parallelism: two ways to use multiple cores

Within a **single worker** that processes multiple games (e.g. a thread pool), we can either:

- **Parallelize over games:** each thread takes a different game and runs the full pipeline (replay → all detectors → collect results). No change to detector-centric vs position-centric; each game still uses one of the two iteration styles on its own position list.
- **Parallelize within one game:** use multiple threads for a single game’s extraction. Here the iteration style matters.

```mermaid
flowchart TB
  subgraph detector_parallel["Detector-centric: parallelize over detectors (one game)"]
    P1[positions]
    P1 --> T1[Thread 1: PinDetector]
    P1 --> T2[Thread 2: SkewerDetector]
    P1 --> T3[Thread 3: CheckDetector]
    P1 --> T4[Thread 4: AttackDetector]
    T1 --> Merge1[Merge occurrence lists]
    T2 --> Merge1
    T3 --> Merge1
    T4 --> Merge1
  end

  subgraph position_parallel["Position-centric: parallelize over position ranges (one game)"]
    P2[positions]
    P2 --> R1[Range 1..M/4]
    P2 --> R2[Range M/4+1..M/2]
    P2 --> R3[Range M/2+1..3M/4]
    P2 --> R4[Range 3M/4+1..M]
    R1 --> T5[Thread 1: all detectors on range 1]
    R2 --> T6[Thread 2: all detectors on range 2]
    R3 --> T7[Thread 3: all detectors on range 3]
    R4 --> T8[Thread 4: all detectors on range 4]
    T5 --> Merge2[Merge occurrence lists]
    T6 --> Merge2
    T7 --> Merge2
    T8 --> Merge2
  end

  style P1 fill:#E3F2FD,stroke:#1565C0
  style P2 fill:#E3F2FD,stroke:#1565C0
  style T1 fill:#FFE0B2,stroke:#E65100
  style T2 fill:#FFE0B2,stroke:#E65100
  style T3 fill:#FFE0B2,stroke:#E65100
  style T4 fill:#FFE0B2,stroke:#E65100
  style R1 fill:#BBDEFB,stroke:#0D47A1
  style R2 fill:#BBDEFB,stroke:#0D47A1
  style R3 fill:#BBDEFB,stroke:#0D47A1
  style R4 fill:#BBDEFB,stroke:#0D47A1
  style Merge1 fill:#E1BEE7,stroke:#7B1FA2
  style Merge2 fill:#E1BEE7,stroke:#7B1FA2
```

- **Detector-centric + parallel over detectors:** Each thread runs one (or a subset of) detectors on the **full** position list. No coordination between threads; merge is trivial (concatenate per-motif lists). Drawback: every thread touches the whole list, so memory bandwidth and cache pressure scale with the number of detector-threads.
- **Position-centric + parallel over position ranges:** Each thread runs **all** detectors on a slice of positions (or pairs). Each thread touches a contiguous slice; better locality and less cross-thread cache contention. Drawback: pair-based detectors need pairs that span the slice boundary (e.g. last position of range 1 and first of range 2); we must either handle boundary pairs in one thread or add a separate pass for pairs.

### Trade-offs under horizontal scaling

| Dimension | Detector-centric (many workers) | Position-centric (many workers) |
|-----------|----------------------------------|----------------------------------|
| **Scale-out** | Add more workers → more games/sec. No difference between iteration styles; both are stateless per game. | Same. |
| **Memory per worker** | One game’s position list per in-flight game. If we run K games in parallel per worker (e.g. thread pool), we hold K lists. ~tens of MB per game (see PARALLELIZING_INDEXING / IN_PROCESS_MODE). | Same per game. Slightly different access pattern doesn’t change the size of the position list. |
| **Parallel games per worker** | Straightforward: submit each game to an executor; each task runs `extract(positions)` with detector-centric loop. No coupling between games. | Same: each game is an independent `extract` with position-centric loop. |
| **Parallel within one game (multi-core worker)** | **Over detectors:** simple. N threads, each runs a subset of detectors on the full list; merge lists at the end. Load balance depends on detector cost (AttackDetector heavier than e.g. CheckDetector). | **Over position ranges:** better locality; each thread touches a subset of positions. Requires handling pair boundaries (AttackDetector) and merging per-position results. More orchestration. |
| **Cache / bandwidth when many workers** | Each worker (or each core) repeatedly scans its game’s list. On a packed node with many workers, total memory bandwidth can become a bottleneck; detector-centric does more full-list scans. | Position-centric reduces full-list scans per game; when we parallelize over positions, each core works on a smaller window. Can reduce per-worker bandwidth and improve throughput when many workers share a node. |
| **Operational simplicity** | One code path: every worker runs the same detector-centric extract. Tuning = worker count, games in flight per worker, and (if needed) detector-level parallelism. | Same single path per worker if we adopt position-centric; tuning is similar. If we add “parallel over positions” inside one game, we add a second parallelism dimension to reason about. |

### Recommendation when scaling horizontally

- **Scale-out (more workers):** Use the same iteration style everywhere; add workers until queue or DB is the bottleneck. Detector-centric is fine and keeps the implementation simple.
- **Scale-up (more cores per worker, parallel within a game):** Prefer **parallel over detectors** (detector-centric) first: minimal code change, trivial merge. If profiling shows that a single game’s extraction is CPU-bound and detector-centric parallelization doesn’t scale (e.g. memory bandwidth bound), consider **position-centric with parallel position ranges** and explicit handling of pair boundaries.
- **Many workers on one node:** If you run a high number of workers per machine, memory bandwidth can dominate. Position-centric (and, if used, parallel-over-positions) tends to reduce redundant scans of the same position list and may allow more workers per node before bandwidth saturates.

---

## When Position-Centric Can Win

Position-centric iteration is more attractive when:

1. **Games are long** and the position list is large, so re-scanning it per detector causes noticeable cache/bandwidth pressure.
2. **We parallelize over positions** (e.g. per-position or per-range tasks) and want each task to touch a small, contiguous slice of data.
3. **We add cost per position** (e.g. parsing FEN into a board once per position and reusing that for all detectors at that position); then “one pass per position” amortizes that cost.
4. **We scale out with many workers per node** and memory bandwidth becomes a bottleneck; position-centric reduces full-list scans per game and can allow more workers per machine before bandwidth saturates (see [Horizontal scaling](#horizontal-scaling-of-index-workers)).

It is less attractive when:

1. **Detectors already short-circuit** (e.g. mate detectors that only look at the last position); the current design avoids unnecessary work inside the detector.
2. **We prefer parallelizing over detectors** (each detector is independent and gets the full list); detector-centric keeps that model simple.
3. **We want to keep a single, simple API** (`detect(List<PositionContext>)`) and let each detector own how it walks the list.

---

## Recommendation (Summary)

- **Keep detector-centric iteration** unless profiling shows that position-list access is a bottleneck (e.g. cache misses or memory bandwidth).
- **If we move to position-centric**, introduce a clear “step” abstraction (single position vs pair) and have the orchestrator drive steps and call detectors per step; use a diagram similar to the “Position-centric” one above to document the intended flow.
- **When scaling horizontally:** Both styles scale out (add workers) equally well. To use multiple cores *within* one worker on a single game, prefer parallel-over-detectors (detector-centric) first; consider position-centric with parallel position ranges only if bandwidth or locality becomes the limit (see [Horizontal scaling](#horizontal-scaling-of-index-workers)).
- **Optimizations that work with both:** Reduce work per position inside detectors (e.g. only consider the last position for checkmate subtypes), and avoid re-parsing FEN when possible (e.g. shared board representation per position). These don’t require changing who iterates.

---

## Diagram Legend

| Color / style | Meaning |
|---------------|--------|
| **Light blue** (`#E3F2FD`, `#BBDEFB`) | Position list or single position / step |
| **Amber / orange** (`#FFE0B2`, `#FFCCBC`) | Detectors |
| **Green** (`#C8E6C9`, `#A5D6A7`) | Inner iteration or single-position detectors |
| **Blue** (`#90CAF9`) | Pair-based detectors |
| **Purple** (`#E1BEE7`) | Output (occurrences) |
| **Indigo** (`#C5CAE9`) | Position-centric “step” invocations |
