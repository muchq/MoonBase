# Plan: Derive Back-Rank Mate, Smothered Mate, and Promotion-with-Checkmate from ATTACK

This document describes how to derive **back_rank_mate**, **smothered_mate**, and **promotion_with_checkmate** at query/response time from ATTACK rows, in the same way that **checkmate**, **fork**, **discovered_attack**, **discovered_check**, and **double_check** are already derived. The goal is a single source of truth (ATTACK) for all mate-related motifs and no separate indexed rows or dedicated detector output for these three subtypes.

---

## Overview: Current vs New Flow

```mermaid
%%{init: {'theme':'base', 'themeVariables': { 'primaryColor':'#E3F2FD', 'primaryTextColor':'#0D47A1', 'lineColor':'#546E7A', 'secondaryColor':'#FFF8E1', 'tertiaryColor':'#E8F5E9'}}}%%
flowchart TB
  subgraph current["Current (index time)"]
    direction TB
    A1[AttackDetector] --> A2[(ATTACK rows)]
    B1[BackRankMateDetector] --> B2[(BACK_RANK_MATE rows)]
    C1[SmotheredMateDetector] --> C2[(SMOTHERED_MATE rows)]
    D1[PromotionWithCheckmateDetector] --> D2[(PROMOTION_WITH_CHECKMATE rows)]
  end

  subgraph new["After change (index time)"]
    direction TB
    E1[AttackDetector] --> E2[ATTACK + mate_type]
    F1[BackRankMateDetector] -.->|"tag only"| E2
    F2[SmotheredMateDetector] -.->|"tag only"| E2
    F3[PromotionWithCheckmateDetector] -.->|"tag only"| E2
    E2 --> E3[(ATTACK rows with mate_type)]
  end

  style A2 fill:#BBDEFB,stroke:#1565C0
  style B2 fill:#FFCC80,stroke:#E65100
  style C2 fill:#FFCC80,stroke:#E65100
  style D2 fill:#FFCC80,stroke:#E65100
  style E3 fill:#A5D6A7,stroke:#2E7D32
  style E2 fill:#C8E6C9,stroke:#388E3C
```

```mermaid
%%{init: {'theme':'base', 'themeVariables': { 'primaryColor':'#E8F5E9', 'primaryTextColor':'#1B5E20'}}}%%
flowchart LR
  subgraph query["Query time (unchanged pattern)"]
    DB[(motif_occurrences)]
    DB --> Read[Read ATTACK + others]
    Read --> Derive[Derive: checkmate, fork, discovered_*, double_check]
    Derive --> Out[Return map without ATTACK]
  end

  subgraph query_new["Query time (after: add 3 derivations)"]
    DB2[(ATTACK with mate_type)]
    DB2 --> Derive2[Derive: checkmate, fork, ... + back_rank_mate, smothered_mate, promotion_with_checkmate]
    Derive2 --> Out2[Return map without ATTACK]
  end

  style DB fill:#BBDEFB,stroke:#1565C0
  style DB2 fill:#BBDEFB,stroke:#1565C0
  style Derive fill:#C8E6C9,stroke:#2E7D32
  style Derive2 fill:#C8E6C9,stroke:#2E7D32
  style Out fill:#E1BEE7,stroke:#7B1FA2
  style Out2 fill:#E1BEE7,stroke:#7B1FA2
```

---

## Current State

- **ATTACK** is the only motif stored for “piece attacks piece” (including check and checkmate). Each row has `attacker`, `target`, `is_discovered`, `is_mate`, etc.
- At **query time**, `GameFeatureDao.queryOccurrences()`:
  - Reads ATTACK rows (and other non-derived motifs) from `motif_occurrences`.
  - **Derives** checkmate, fork, discovered_attack, discovered_check, double_check from ATTACK.
  - Removes ATTACK from the returned map (it is an internal primitive).
- **Back-rank mate**, **smothered mate**, and **promotion_with_checkmate** are today produced by dedicated detectors (`BackRankMateDetector`, `SmotheredMateDetector`, `PromotionWithCheckmateDetector`) and **stored as separate motif rows** (BACK_RANK_MATE, SMOTHERED_MATE, PROMOTION_WITH_CHECKMATE).

---

## Why Derivation Requires Extra Data

The existing derived motifs use only ATTACK columns:

- **Checkmate**: `is_mate = true`.
- **Fork**: same (moveNumber, side, attacker), 2+ targets.
- **Discovered check**: `is_discovered = true` and target is king.

The three mate subtypes cannot be inferred from ATTACK alone:

| Subtype                 | What we need beyond (attacker, target, is_mate)        |
|-------------------------|--------------------------------------------------------|
| Back-rank mate          | King on back rank (from target) **and** escape blocked by own pieces (needs board). |
| Smothered mate          | Attacker is knight (from attacker) **and** king has no empty adjacent square (needs board). |
| Promotion with checkmate| Last move was promotion **and** promoted piece gives mate (needs move/board).        |

So we cannot derive these subtypes at query time from current ATTACK columns only. We need **one extra piece of information per mate ATTACK** at index time, then derivation is a simple filter.

---

## Approach: Store Mate Subtype on ATTACK Rows

1. **Add a `mate_type` column** to `motif_occurrences` (nullable). Values: `'BACK_RANK_MATE'`, `'SMOTHERED_MATE'`, `'PROMOTION_WITH_CHECKMATE'`, or `NULL` (generic checkmate).
2. **At index time**, when writing ATTACK rows with `is_mate = true`, set `mate_type` by matching the checkmate to the outputs of the existing back-rank, smothered, and promotion-with-checkmate detectors (same game, same move number / position). No new detector logic; reuse current detectors only to **classify** the mate ATTACK.
3. **At query time**, derive the three motifs by filtering ATTACK rows where `is_mate` and `mate_type` is the corresponding value; emit one derived occurrence per such row (same shape as current checkmate derivation). Do **not** read stored rows for BACK_RANK_MATE, SMOTHERED_MATE, PROMOTION_WITH_CHECKMATE (exclude them from the query like FORK/CHECKMATE).
4. **Stop inserting** separate rows for BACK_RANK_MATE, SMOTHERED_MATE, PROMOTION_WITH_CHECKMATE. The three detectors still run so we have a list of (moveNumber, …) for each subtype; that list is used only to set `mate_type` on the corresponding mate ATTACK row.

Result: one ATTACK row per checkmate (with optional `mate_type`); back_rank_mate, smothered_mate, and promotion_with_checkmate exist only as derived views of ATTACK, consistent with checkmate/fork/discovered_check.

```mermaid
flowchart TB
  subgraph index["Index time: detectors → insert"]
    PGN[PGN + replayed positions]
    PGN --> AD[AttackDetector]
    PGN --> BR[BackRankMateDetector]
    PGN --> SM[SmotheredMateDetector]
    PGN --> PW[PromotionWithCheckmateDetector]

    AD --> ATTACK_LIST["ATTACK occurrences<br/>(is_mate on mate rows)"]
    BR --> BR_LIST["BACK_RANK_MATE<br/>occurrences"]
    SM --> SM_LIST["SMOTHERED_MATE<br/>occurrences"]
    PW --> PW_LIST["PROMOTION_WITH_CHECKMATE<br/>occurrences"]

    ATTACK_LIST --> MERGE["For each mate ATTACK:<br/>match moveNumber to BR/SM/PW lists<br/>→ set mate_type"]
    BR_LIST -.->|match| MERGE
    SM_LIST -.->|match| MERGE
    PW_LIST -.->|match| MERGE

    MERGE --> INSERT["Insert only ATTACK rows<br/>(with mate_type);<br/>do not insert BR/SM/PW rows)"]
    INSERT --> DB[(motif_occurrences)]
  end

  style AD fill:#BBDEFB,stroke:#1565C0
  style ATTACK_LIST fill:#BBDEFB,stroke:#1565C0
  style BR fill:#FFE0B2,stroke:#EF6C00
  style SM fill:#FFE0B2,stroke:#EF6C00
  style PW fill:#FFE0B2,stroke:#EF6C00
  style MERGE fill:#C8E6C9,stroke:#2E7D32
  style INSERT fill:#C8E6C9,stroke:#2E7D32
  style DB fill:#E1BEE7,stroke:#7B1FA2
```

---

## Implementation Steps

### 1. Schema and DTO

- **Migration**: `ALTER TABLE motif_occurrences ADD COLUMN IF NOT EXISTS mate_type VARCHAR(32)`.
- **OccurrenceRow** (or the internal row type used when reading): add optional `mateType` (e.g. `String` or enum) so that when we read ATTACK rows we have `mate_type` for derivation.
- **Insert**: extend the INSERT statement and `insertOccurrences` to accept and write `mate_type` for ATTACK rows (nullable).

### 2. Index Time: Set `mate_type` When Inserting ATTACK

- In **GameFeatureDao.insertOccurrences** (or the worker code that builds the insert payload): when iterating over ATTACK occurrences, for each occurrence with `isMate() == true`:
  - Determine `mate_type` by checking whether the same (gameUrl, moveNumber) [or (gameUrl, ply)] appears in:
    - `occurrences.get(Motif.BACK_RANK_MATE)`
    - `occurrences.get(Motif.SMOTHERED_MATE)`
    - `occurrences.get(Motif.PROMOTION_WITH_CHECKMATE)`
  - At most one of these should match (subtypes are mutually exclusive). Set that as `mate_type`; if none match, leave `mate_type` NULL.
- When building the batch of rows to insert, **do not** add rows for BACK_RANK_MATE, SMOTHERED_MATE, PROMOTION_WITH_CHECKMATE to the insert list. Only ATTACK (and other non-derived motifs) are written; ATTACK rows carry `mate_type`.

So the three detectors continue to run and their results are used only to tag mate ATTACKs; no separate rows for those three motifs are persisted.

### 3. Query Time: Exclude and Derive

```mermaid
flowchart LR
  subgraph read["1. Read from DB"]
    DB[(motif_occurrences)]
    DB --> ATTACK["ATTACK rows<br/>(incl. mate_type)"]
    DB --> OTHER["PIN, SKEWER, CHECK,<br/>PROMOTION, ..."]
  end

  subgraph derive["2. Derive from ATTACK"]
    ATTACK --> D1[checkmate<br/>is_mate]
    ATTACK --> D2[fork<br/>2+ targets same attacker]
    ATTACK --> D3[discovered_attack<br/>is_discovered]
    ATTACK --> D4[discovered_check<br/>discovered + king]
    ATTACK --> D5[double_check<br/>2+ king attackers]
    ATTACK --> D6[back_rank_mate<br/>is_mate + mate_type]
    ATTACK --> D7[smothered_mate<br/>is_mate + mate_type]
    ATTACK --> D8[promotion_with_checkmate<br/>is_mate + mate_type]
  end

  subgraph out["3. Return"]
    OTHER --> MAP[Occurrence map]
    D1 --> MAP
    D2 --> MAP
    D3 --> MAP
    D4 --> MAP
    D5 --> MAP
    D6 --> MAP
    D7 --> MAP
    D8 --> MAP
    MAP --> API["Response<br/>(no ATTACK key)"]
  end

  style DB fill:#E1BEE7,stroke:#7B1FA2
  style ATTACK fill:#BBDEFB,stroke:#1565C0
  style D1 fill:#A5D6A7,stroke:#2E7D32
  style D2 fill:#A5D6A7,stroke:#2E7D32
  style D3 fill:#A5D6A7,stroke:#2E7D32
  style D4 fill:#A5D6A7,stroke:#2E7D32
  style D5 fill:#A5D6A7,stroke:#2E7D32
  style D6 fill:#80CBC4,stroke:#00695C
  style D7 fill:#80CBC4,stroke:#00695C
  style D8 fill:#80CBC4,stroke:#00695C
  style API fill:#FFF9C4,stroke:#F9A825
```

- In **GameFeatureDao.queryOccurrences**:
  - **Exclude** BACK_RANK_MATE, SMOTHERED_MATE, PROMOTION_WITH_CHECKMATE from the `WHERE motif NOT IN (...)` list so we never read previously stored rows for them (same as FORK, CHECKMATE, etc.).
  - When reading ATTACK rows, read the new `mate_type` column into the in-memory row type.
  - After deriving **checkmate** from ATTACK (`is_mate`), add three new derivations:
    - **back_rank_mate**: from ATTACK rows where `is_mate` and `mate_type == "BACK_RANK_MATE"` (or equivalent). Build one occurrence per row (same shape as `deriveCheckmateOccurrences`: gameUrl, motif name, moveNumber, side, description, movedPiece, attacker, target, is_discovered=false, is_mate=true, pinType=null).
    - **smothered_mate**: same, for `mate_type == "SMOTHERED_MATE"`.
    - **promotion_with_checkmate**: same, for `mate_type == "PROMOTION_WITH_CHECKMATE"`.
  - Add these to the motif map with keys `"back_rank_mate"`, `"smothered_mate"`, `"promotion_with_checkmate"` (lowercase to match existing convention).

### 4. ChessQL / Query Layer

- **SqlCompiler** (and DataFusionSqlCompiler) already treat these motifs as first-class (e.g. `motif(back_rank_mate)`). No change needed if the API returns derived occurrences under those names.
- **QueryController** / response mapping: ensure that when we return occurrences by motif name, the derived lists for back_rank_mate, smothered_mate, promotion_with_checkmate are included (they will be, if we put them in the same map returned by `queryOccurrences`).

### 5. Backfill and Cleanup

- **Existing data**: Games indexed before this change have BACK_RANK_MATE, SMOTHERED_MATE, PROMOTION_WITH_CHECKMATE as stored rows and ATTACK rows without `mate_type`. Options:
  - **Option A**: Leave old data as-is. Query logic: when building the occurrence map, if we have stored rows for back_rank_mate/smothered_mate/promotion_with_checkmate (e.g. from an older schema that didn’t exclude them), merge them into the result; for games that have ATTACK + mate_type, use derivation only. That implies we do **not** exclude the three from the SELECT initially, and we either (i) derive only when mate_type is present and no stored rows, or (ii) prefer derived over stored when both exist. Simplest: exclude the three from SELECT (so old stored rows are never read); then old games simply won’t show these subtypes until re-indexed.
  - **Option B**: One-time backfill that, for each game that has ATTACK rows with is_mate but no mate_type, runs the three detectors (from stored PGN), matches mate ATTACKs to detector output, and updates `mate_type` on those ATTACK rows; then deletes stored BACK_RANK_MATE/SMOTHERED_MATE/PROMOTION_WITH_CHECKMATE rows for that game.
- **Cleanup**: Once all relevant games have ATTACK.mate_type set (or we accept that old games lack subtype), we can remove the three dedicated detectors from the **index pipeline** only if we are comfortable losing subtype for games that haven’t been re-indexed. Alternatively, keep the detectors forever at index time purely to set `mate_type` (no separate inserts).

### 6. Tests and E2E

- **GameFeatureDaoTest**: extend tests that insert ATTACK + mate to also set mate_type and assert that queryOccurrences returns derived back_rank_mate / smothered_mate / promotion_with_checkmate as appropriate.
- **E2E**: ensure motif(back_rank_mate), motif(smothered_mate), motif(promotion_with_checkmate) still return the expected games (Opera Game etc.) after the change. If we exclude the three from the SELECT and don’t backfill, re-index those games once so ATTACK rows get mate_type.

---

## Summary Table

| Motif                    | Today                         | After change                          |
|--------------------------|-------------------------------|----------------------------------------|
| checkmate                | Derived from ATTACK (is_mate) | Unchanged                              |
| back_rank_mate           | Stored (dedicated detector)   | Derived from ATTACK (is_mate + mate_type) |
| smothered_mate           | Stored (dedicated detector)   | Derived from ATTACK (is_mate + mate_type) |
| promotion_with_checkmate | Stored (dedicated detector)   | Derived from ATTACK (is_mate + mate_type) |

Index time: ATTACK rows with `is_mate` get `mate_type` set using existing detector output; no rows inserted for the three subtypes. Query time: three new derivation branches from ATTACK, same pattern as checkmate; stored rows for the three subtypes are no longer read (excluded from SELECT).

```mermaid
flowchart TB
  subgraph source["Single source of truth"]
    A[ATTACK rows<br/>attacker, target, is_mate, mate_type]
  end

  subgraph derived["Derived at query time"]
    A --> CM[checkmate]
    A --> BR[back_rank_mate]
    A --> SM[smothered_mate]
    A --> PW[promotion_with_checkmate]
    A --> F[fork]
    A --> DA[discovered_attack]
    A --> DC[discovered_check]
    A --> D2[double_check]
  end

  style A fill:#BBDEFB,stroke:#1565C0
  style CM fill:#A5D6A7,stroke:#2E7D32
  style BR fill:#80CBC4,stroke:#00695C
  style SM fill:#80CBC4,stroke:#00695C
  style PW fill:#80CBC4,stroke:#00695C
  style F fill:#A5D6A7,stroke:#2E7D32
  style DA fill:#A5D6A7,stroke:#2E7D32
  style DC fill:#A5D6A7,stroke:#2E7D32
  style D2 fill:#A5D6A7,stroke:#2E7D32
```
