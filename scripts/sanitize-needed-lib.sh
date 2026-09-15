# Shared by scripts/sanitize-needed (CI) and scripts/test-sanitize-needed.
# Sourced, not executed. Keep this free of side effects beyond the definitions.
#
# Pathspecs whose change means the sanitize matrix should run: first-party C++
# (and the BUILD / proto / .bzl files that shape it), plus the config and
# toolchain class already tracked for full builds — a .bazelrc or llvm pin
# change can alter sanitizer codegen without touching a .cc.

SCRIPT_DIR=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
# shellcheck source=diff-build-lib.sh
source "$SCRIPT_DIR/diff-build-lib.sh"

SANITIZE_PATHSPECS=(
  "${FULL_BUILD_PATHSPECS[@]}"
  ':(glob)domains/**/*.c'
  ':(glob)domains/**/*.cc'
  ':(glob)domains/**/*.cpp'
  ':(glob)domains/**/*.cxx'
  ':(glob)domains/**/*.h'
  ':(glob)domains/**/*.hh'
  ':(glob)domains/**/*.hpp'
  ':(glob)domains/**/*.hxx'
  ':(glob)domains/**/BUILD.bazel'
  ':(glob)domains/**/*.bzl'
  ':(glob)domains/**/*.proto'
)

# Paths between merge-base(base, HEAD) and HEAD that force the sanitize
# matrix, one per line, or empty. Caller must have already required the base;
# git diff failures propagate. Three-dot form matches the other path-gated
# jobs in branch.yml (microgpt, otel, deploy-macos).
paths_needing_sanitize() {
  local base=$1
  git diff --name-only "${base}...HEAD" -- "${SANITIZE_PATHSPECS[@]}"
}
