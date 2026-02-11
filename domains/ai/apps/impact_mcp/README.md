# impact-mcp

**A local-first AI toolkit to help engineers amplify impact and grow in their role.**

`impact-mcp` is a tool designed to capture evidence of your engineering impact, clarify role expectations through rubrics, and provide a local AI assistant via the Model Context Protocol (MCP) to help you communicate your contributions effectively.

It integrates with Claude (via MCP) to act as a career coach and impact tracker directly within your workflow.

## Features

*   **Evidence Collection**: Log achievements manually or automatically pull them from integrations (GitHub, Jira, Slack, Google Docs).
*   **Role Rubrics**: Define and track progress against career ladders and expectations (e.g., Tech Lead, Senior Engineer).
*   **Local-First Privacy**: All data is stored locally on your machine.
*   **Claude Integration**: Exposes tools to Claude via MCP to query your impact, identify gaps, and draft performance reviews.
*   **Automation**: Set up cron jobs to automatically pull data from connected services.

## Installation

### Bazel (Recommended)

To build and run the application using Bazel:

```bash
bazel run //domains/ai/apps/impact_mcp:impact-mcp
```

### Cargo (Alternative)

If you have a Rust toolchain installed, you can also build directly with Cargo:

```bash
cd domains/ai/apps/impact_mcp
cargo install --path .
```

Or run directly:

```bash
cargo run
```

## Usage

### 1. Setup

First, configure the integration with Claude or Codex. This installs necessary MCP server configurations and skills.

```bash
# Default setup (installs Claude skills to ~/.claude/skills and Codex skills to ~/.codex/skills)
impact-mcp setup

# Custom skill installation directories
impact-mcp setup --claude-skills-dir /path/to/claude/skills --codex-skills-dir /path/to/codex/skills
```

This will:
1.  Install skill files (markdown prompts) to `~/.claude/skills` (and configure `~/.claude/settings.json`).
2.  Install skill files to `~/.codex/skills`.

Follow the instructions to restart your MCP client (e.g., Claude Code).

### 2. Tracking Projects

Track your active projects, role, status, and completion to generate meaningful updates.

```bash
# Add a new project
impact-mcp projects add \
  --name "Payment Service Migration" \
  --role "Tech Lead" \
  --status "Execution" \
  --completion 0.4 \
  --jira "PAY" \
  --repos "pay-serv,pay-lib"

# Update an existing project
impact-mcp projects update \
  --name "Payment Service Migration" \
  --status "Delivered" \
  --completion 1.0

# List tracked projects
impact-mcp projects list
```

### 3. Managing Rubrics

Initialize a default rubric or load a custom one to define role expectations.

```bash
# Initialize default rubric
impact-mcp rubric init

# Show current rubric
impact-mcp rubric show

# Load a custom rubric
impact-mcp rubric load --path /path/to/rubric.yaml
```

### 4. Collecting Evidence

You can add evidence manually or pull it from integrations.

**Manual Entry:**

```bash
impact-mcp evidence add \
  --summary "Led the migration to Kubernetes, reducing deployment time by 50%" \
  --source manual \
  --rubric-tags "infrastructure,velocity" \
  --archetype-tags "tech_lead"
```

**Automated Pull:**
You can use an AI agent (Claude or Codex) to intelligently pull and summarize evidence from your projects, or use the built-in connectors.

```bash
# Use Claude to pull evidence for your tracked projects (Recommended)
impact-mcp pull --claude

# Use built-in connectors (GitHub, Jira, Slack)
impact-mcp pull
```

**List Evidence:**

```bash
impact-mcp evidence list
```

### 5. Weekly Updates

Generate a weekly status update template based on your tracked projects and recent evidence.

```bash
impact-mcp weekly-update
```

### 6. Automatic Sync (macOS)

Set up an hourly cron job to pull data from integrations automatically.

```bash
impact-mcp setup-cron
```

## Configuration

Data is stored in your local data directory (default: `~/.local/share/impact-mcp` on Linux, `~/Library/Application Support/impact-mcp` on macOS).

You can override this by setting the `IMPACT_MCP_DATA_DIR` environment variable.

### Integrations

To enable automated pulls, set the following environment variables:

*   **GitHub**: `IMPACT_MCP_GITHUB_TOKEN`
*   **Jira**: `IMPACT_MCP_JIRA_TOKEN` and `IMPACT_MCP_JIRA_URL`
*   **Slack**: `IMPACT_MCP_SLACK_TOKEN`
