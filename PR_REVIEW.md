# Review Comments for PR #961

## Major Issues

### 1. Stale System Prompt in `Agent`
**File:** `domains/ai/apps/impact_mcp/src/agent/mod.rs`

The `Agent::with_llm` constructor builds the system prompt once:
```rust
let system_prompt = llm::build_system_prompt(&store, &rubric);
Self {
    // ...
    llm: Some(LlmClient::new(system_prompt)),
}
```
If the evidence store changes during the session (e.g., if we add commands to add evidence interactively, or if `run_pull` is integrated into the chat loop), the LLM's system prompt will remain stale and won't reflect the new data.

**Suggestion:**
Instead of baking the system prompt into `LlmClient` at construction, generate the context dynamically for each request, or provide a way to refresh the system prompt when the store changes.

### 2. Robustness of `EvidenceStore::open`
**File:** `domains/ai/apps/impact_mcp/src/evidence/store.rs`

In `EvidenceStore::open`, iterating over files propagates errors immediately:
```rust
for entry in fs::read_dir(&evidence_dir).map_err(StoreError::Io)? {
    let entry = entry.map_err(StoreError::Io)?;
    // ...
    let data = fs::read_to_string(&path).map_err(StoreError::Io)?;
    let card: EvidenceCard = serde_json::from_str(&data).map_err(StoreError::Parse)?;
}
```
If a single file is corrupted or unreadable (e.g., permissions), the entire application will fail to start.

**Suggestion:**
Log errors for individual files and continue loading the rest. Use `eprintln!` or `tracing::warn!` to inform the user about skipped files.

## Minor Issues

### 3. Duplicate Archetype Parsing Logic
**File:** `domains/ai/apps/impact_mcp/src/main.rs`

In `run_evidence` (`EvidenceCommand::Add`), the matching logic for archetypes is manually implemented:
```rust
match s.trim().to_lowercase().as_str() {
    "tech_lead" | "techlead" => Some(Archetype::TechLead),
    // ...
}
```
This logic likely belongs in `Archetype` itself (e.g., implementing `FromStr`), so it can be reused and tested independently.

### 4. Hard Panic on Mutex Poisoning
**File:** `domains/ai/apps/impact_mcp/src/agent/mod.rs`

The use of `self.history.lock().unwrap()` will panic if the mutex is poisoned. While this is less critical in a short-lived CLI, it would be safer to handle the error or use `expect("mutex poisoned")` to make the failure mode explicit.

### 5. Inconsistent Command Loop
**File:** `domains/ai/apps/impact_mcp/src/main.rs`

`run_chat` uses a loop for interaction, but other modes like `run_status` run once. If the intention is to allow interactive refinement of the status report or packet, those might benefit from a loop or being sub-modes of the chat agent.
