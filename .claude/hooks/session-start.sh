#!/bin/bash
# SessionStart hook for Claude Code on the web.
#
# A fresh web container can't build this repo. Two things are missing, and both
# fail in ways that look like something else:
#
#   1. There is no bazel on PATH. Bazel is the build system for every language
#      here (docs/BUILD_AND_IDE.md), so `bazel test` is "command not found"
#      until bazelisk is installed.
#   2. The egress proxy 403s GitHub *source archives*, which is how most BCR
#      modules fetch their sources. Without the git-based overrides, every
#      Boost archive fails — and so every Beast target, libpng, portrait,
#      opentelemetry-cpp, and cel-spec, which gazelle needs, which means even
#      `bazel run //:buildifier` and `scripts/format-all` fail. The repo ships
#      scripts/make-git-overrides.sh for exactly this; see CLAUDE.md and
#      docs/BUILD_AND_IDE.md.
#
# Both are one-time costs: the container image is cached after this hook
# completes, so later sessions start against a warm tree.
#
# Idempotent and non-interactive — safe to re-run by hand:
#   CLAUDE_CODE_REMOTE=true .claude/hooks/session-start.sh
set -euo pipefail

# Local checkouts have their own toolchains; this is only for the web sandbox.
if [ "${CLAUDE_CODE_REMOTE:-}" != "true" ]; then
  exit 0
fi

REPO="${CLAUDE_PROJECT_DIR:-$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)}"
cd "$REPO"

# Repo-scoped rather than the docs' bare $HOME/bazel-overrides. The override
# set is derived from *this* repo's MODULE.bazel.lock, so a session that also
# checks out another Bazel repo (smithy-cpp, say) would otherwise find a
# populated directory, skip the build, and silently point MoonBase at another
# project's modules — missing libpng and raylib, and failing to fetch them with
# the same 403 this hook exists to prevent.
OVERRIDES_DIR="$HOME/bazel-overrides-moonbase"
OVERRIDES_RC="$OVERRIDES_DIR/overrides.bazelrc"

echo "==> MoonBase session setup"

# --- 1. bazel (via bazelisk, which reads .bazelversion) ------------------------
if command -v bazel >/dev/null 2>&1; then
  echo "    bazel: already on PATH ($(command -v bazel))"
else
  echo "    bazel: installing bazelisk"
  npm install -g @bazel/bazelisk >/dev/null
  echo "    bazel: $(command -v bazel)"
fi

# --- 2. git overrides for the proxy-blocked archives --------------------------
# Non-fatal on purpose: this reaches ~77 upstream repos, and one network flake
# should not stop the session from starting. A docs- or Go-only session is
# perfectly usable without it. The warning names the fix rather than leaving a
# later `bazel build` to fail with a 403 nobody connects to this step.
if [ -f "$OVERRIDES_RC" ]; then
  echo "    overrides: already built ($OVERRIDES_RC)"
elif scripts/make-git-overrides.sh "$OVERRIDES_DIR"; then
  echo "    overrides: built $OVERRIDES_RC"
else
  echo "!!! overrides: FAILED. C++ targets that need Boost/Beast, libpng, otel," >&2
  echo "!!! or buildifier will fail to fetch with a 403. Re-run:" >&2
  echo "!!!   scripts/make-git-overrides.sh $OVERRIDES_DIR" >&2
fi

# --- 3. wire the overrides into .bazelrc.user ---------------------------------
# .bazelrc try-imports this file and .gitignore excludes it, so it stays out of
# version control. A bazelrc `import` expands neither ~ nor $VARS, so the
# absolute path is written literally. --lockfile_mode=off is required:
# overridden modules drop out of lockfile verification, and without it every
# run dirties the checked-in MODULE.bazel.lock.
if [ -f "$OVERRIDES_RC" ]; then
  touch .bazelrc.user
  grep -qxF 'common --lockfile_mode=off' .bazelrc.user \
    || echo 'common --lockfile_mode=off' >> .bazelrc.user
  grep -qxF "import $OVERRIDES_RC" .bazelrc.user \
    || echo "import $OVERRIDES_RC" >> .bazelrc.user
  echo "    .bazelrc.user: wired"
fi

# --- 4. language toolchains that are already installed but cold ---------------
# Best-effort: these only warm caches, so a failure is not worth a red session.
if command -v go >/dev/null 2>&1; then
  go mod download >/dev/null 2>&1 && echo "    go: modules downloaded" \
    || echo "    go: module download skipped"
fi

if [ -f domains/games/apps/1d4_web/package.json ]; then
  # npm install rather than ci: the container caches after this hook, and
  # install reuses what is already there on a re-run.
  (cd domains/games/apps/1d4_web && npm install --silent >/dev/null 2>&1) \
    && echo "    1d4_web: npm dependencies installed" \
    || echo "    1d4_web: npm install skipped"
fi

echo "==> done"
