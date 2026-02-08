# impact-mcp

A local-first AI agent that helps engineers amplify their impact and grow in their role. The core idea: doing high-impact work leads to better project outcomes *and* naturally builds the visibility needed for career progression. impact-mcp captures what you actually accomplish, surfaces gaps in how you work, and helps you communicate your contributions clearly — for both better results today and a stronger case for promotion tomorrow.

**Location:** `domains/ai/apps/impact_mcp`

## Features

### Evidence Collection

Evidence is the core unit of the system. Each `EvidenceCard` records a single piece of impact: a source, a summary, confidence score, rubric tags, archetype tags, an optional link, and free-text excerpts. Cards are persisted as JSON in `~/.local/share/impact-mcp/evidence/` (overridable via `IMPACT_MCP_DATA_DIR`).

Evidence can be added manually or pulled automatically from integrations:

```bash
impact-mcp evidence add --summary "Led migration of payments service to async queue" \
  --source github --rubric_tags scope,leverage --link https://github.com/org/repo/pull/123

impact-mcp pull   # pull from all configured connectors
impact-mcp evidence list
```

### Impact Readiness Scoring

The `readiness` module computes a score across rubric dimensions based on evidence coverage. Each dimension is classified as None / Weak / Moderate / Strong depending on how many cards reference it. Gaps (under-covered dimensions) are surfaced with actionable suggestions.

```bash
impact-mcp readiness
```

### Rubric Customization

Rubrics are versioned YAML files that define the dimensions (axes) against which evidence is measured. The default rubric ships with six dimensions: **Scope**, **Leverage**, **Influence**, **Quality**, **Operations**, and **Strategy**.

```bash
impact-mcp rubric init              # write default rubric to data dir
impact-mcp rubric show              # display active rubric
impact-mcp rubric load ./my.yaml    # load a custom rubric
```

Example rubric YAML:

```yaml
name: "My Company Staff"
version: "1.0"
dimensions:
  - key: scope
    label: Scope
    description: "Impact beyond immediate team"
    weight: 1.0
  - key: leverage
    label: Leverage
    description: "Multiplying others' effectiveness"
    weight: 1.5
  - key: ops
    label: Operations
    description: "Reliability, on-call diligence, incident response"
    weight: 1.0
```

### Staff Archetypes

Five archetypes model how Staff-level impact is expressed. They are non-exclusive — most engineers exhibit a blend:

| Archetype | Focus |
|---|---|
| **Tech Lead** | Delivery-oriented, drives outcomes across teams |
| **Architect** | Sets technical direction and long-term strategy |
| **Problem Solver** | Tackles ambiguous, novel, high-risk challenges |
| **Operator** | Ensures reliability, quality, systemic risk reduction |
| **Mentor** | Multiplies others' effectiveness |

```bash
impact-mcp archetypes
```

### Interactive Chat

The `chat` command opens a REPL grounded in your evidence store, readiness snapshot, and archetype profile. If an LLM API key is configured, freeform questions are forwarded to the model; otherwise, only built-in commands are available.

```bash
impact-mcp chat
```

Built-in chat commands (no LLM required):

- `What rubric am I using?`
- `What Staff archetypes am I currently showing?`
- `Which archetype should I lean into?`
- `What am I missing?`
- `Update my promotion packet`
- `Draft my weekly status update`
- `Explain my readiness score`
- `help` / `?`

### Output Generation

```bash
impact-mcp status    # draft a weekly status update from recent evidence
impact-mcp packet    # generate a promotion packet section
```

### LLM Integration

impact-mcp uses the `genai` crate to support multiple providers. Set the appropriate API key and optionally override the model:

| Provider | API Key Env Var |
|---|---|
| Anthropic (default) | `ANTHROPIC_API_KEY` |
| OpenAI | `OPENAI_API_KEY` |
| Google Gemini | `GEMINI_API_KEY` |

```bash
IMPACT_MCP_MODEL=claude-opus-4-6 impact-mcp chat
```

Default model: `claude-sonnet-4-5-20250929`.

## Integrations

### Google Docs (MCP)

Fully implemented. Spawns a `google-docs-mcp` MCP server as a child process and pulls recently modified docs, searches for RFCs / design docs / post-mortems, and reads content from the top results.

**Setup:**

```bash
npm install -g google-docs-mcp   # or equivalent
export IMPACT_MCP_GDOCS_MCP_CMD=npx
export IMPACT_MCP_GDOCS_MCP_ARGS="google-docs-mcp"
# Configure Google OAuth credentials per the MCP server's documentation
impact-mcp pull
```

### GitHub, Jira, Slack

Connectors are scaffolded with configuration detection but evidence pulling is not yet implemented (see Roadmap). Set tokens to enable detection:

```bash
export IMPACT_MCP_GITHUB_TOKEN=...
export IMPACT_MCP_JIRA_TOKEN=...
export IMPACT_MCP_JIRA_URL=https://mycompany.atlassian.net
export IMPACT_MCP_SLACK_TOKEN=...
```

## Configuration Reference

| Variable | Default | Description |
|---|---|---|
| `IMPACT_MCP_DATA_DIR` | `~/.local/share/impact-mcp/` | Local data directory |
| `IMPACT_MCP_MODEL` | `claude-sonnet-4-5-20250929` | LLM model override |
| `IMPACT_MCP_GITHUB_TOKEN` | — | GitHub personal access token |
| `IMPACT_MCP_JIRA_TOKEN` | — | Jira API token |
| `IMPACT_MCP_JIRA_URL` | — | Jira base URL |
| `IMPACT_MCP_SLACK_TOKEN` | — | Slack API token |
| `IMPACT_MCP_GDOCS_MCP_CMD` | `npx` | MCP server command |
| `IMPACT_MCP_GDOCS_MCP_ARGS` | `google-docs-mcp` | MCP server arguments |

## Building

```bash
bazel build //domains/ai/apps/impact_mcp/...
bazel test //domains/ai/apps/impact_mcp/...
bazel run //domains/ai/apps/impact_mcp:impact-mcp -- chat
```

## Implementation Notes

- **Data model:** Evidence cards are identified by UUID, tagged with rubric and archetype axes, and stored as line-delimited JSON.
- **Readiness scoring:** Weighted average of per-dimension coverage levels (None=0, Weak=0.25, Moderate=0.6, Strong=1.0) multiplied by dimension weights.
- **LLM context:** The system prompt is built fresh each session from the full evidence store (up to 50 cards shown), readiness snapshot, and archetype profile.
- **MCP client:** `src/mcp/mod.rs` wraps the `rmcp` crate to spawn MCP servers over stdio using `TokioChildProcess`.
- **Conversation history:** Last 20 turns are kept in memory for multi-turn chat.

## Roadmap

- [ ] **GitHub connector** — pull authored/reviewed PRs, RFC discussions, cross-repo contributions, release participation
- [ ] **Jira connector** — pull epic ownership, cross-team tickets, roadmap artifacts
- [ ] **Slack connector** — pull alignment threads, mentorship moments, technical decision discussions
- [ ] **Incremental evidence refresh** — track last-pulled timestamp per connector to avoid re-importing
- [ ] **Duplicate detection** — deduplicate cards across sources (currently only Gdocs deduplicates by link)
- [ ] **Live system prompt refresh** — rebuild LLM context when evidence changes mid-session
- [ ] **ArchetypeProfile persistence** — persist explicit archetype selections across sessions
- [ ] **Confidence auto-scoring** — infer confidence from evidence richness rather than hardcoding per-source defaults
