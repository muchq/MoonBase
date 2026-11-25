# Shared by scripts/sanitize-needed (CI) and scripts/test-sanitize-needed.
# Sourced, not executed. Keep this free of side effects beyond the definitions.
#
# Pathspecs whose change means the sanitize matrix should run: first-party C++
# (and the BUILD / proto / .bzl files that shape it), plus the config and
# toolchain class already tracked for full builds — a .bazelrc or llvm pin
# change can alter sanitizer codegen without touching a .cc.
#
# oci.MODULE.bazel is in FULL_BUILD_PATHSPECS (image digests reshape every
# linux_oci_* consumer for bazel-diff) but is excluded here: a Java/base
# digest pin does not change C++ sanitizer codegen, and running the matrix
# for it only exposes TSan to unrelated CI ASLR flakes.

SCRIPT_DIR=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
# shellcheck source=diff-build-lib.sh
source "$SCRIPT_DIR/diff-build-lib.sh"

# The subset that makes bazel-diff's impact set untrustworthy for the sanitize
# matrix, as it is for diff-build; a change here sanitizes every test in the
# universe rather than the impacted ones. The bazel/ entries reach tests
# through --run_under and register_toolchains, which are not dependency edges,
# so bazel-diff impacts nothing when they change.
SANITIZE_FULL_PATHSPECS=(
  "${FULL_BUILD_PATHSPECS[@]}"
  ':(exclude)bazel/oci.MODULE.bazel'
  bazel/BUILD.bazel
  bazel/run_without_aslr.sh
  'bazel/toolchains/*'
)

SANITIZE_PATHSPECS=(
  "${SANITIZE_FULL_PATHSPECS[@]}"
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

# "full" when a change since merge-base(base, HEAD) means every sanitizable
# test runs, else "diff": only those bazel-diff says are impacted. A diff git
# cannot compute is "full".
sanitize_mode() {
  local base=$1 paths
  paths=$(git diff --name-only "${base}...HEAD" -- "${SANITIZE_FULL_PATHSPECS[@]}" 2>/dev/null) \
    || paths=unknown
  if [ -n "$paths" ]; then echo full; else echo diff; fi
}

# Every test the sanitize matrix may run: first-party cc_tests, minus the
# DB-gated ones (no database in that job, so they would only skip), those
# tagged no-sanitize (the config filters them, and a selection of only those
# would leave bazel nothing to test, which it fails), and the build-cost class
# named in branch.yml's sanitize comment. Adding a line REMOVES a test from all
# three sanitizers.
SANITIZE_UNIVERSE_QUERY='kind(cc_test, //domains/...)
  except attr(env_inherit, "_TEST_DB_URL", kind(cc_test, //domains/...))
  except attr(tags, "\bno-sanitize\b", kind(cc_test, //domains/...))
  except (
  //domains/games/apis/games_hub/... union
  //domains/games/apis/one_d4_worker:chess_com_archive_test union
  //domains/games/apis/one_d4_worker:metrics_test union
  //domains/platform/libs/aura/... union
  //domains/platform/libs/futility/otel/... union
  //domains/graphics/apis/portrait/...)'

# Writes the tests to sanitize to <out>, one label per line. <impacted> is
# bazel-diff's impact set, or "-" for the whole universe. An empty universe
# fails: the query has stopped matching, and reading that as "nothing to run"
# would report green. So does an unreadable impact file. An empty selection
# from a non-empty universe is a change that reaches no sanitizable test, and
# is fine.
select_sanitize_targets() { # select_sanitize_targets <universe> <impacted|-> <out>
  local universe=$1 impacted=$2 out=$3 labels
  labels=$(grep '^//' "$universe" || true)
  if [ -z "$labels" ]; then
    echo "ERROR: sanitize universe is empty; refusing a vacuous pass" >&2
    return 1
  fi
  if [ "$impacted" = - ]; then
    printf '%s\n' "$labels" > "$out"
  else
    { grep -Fx -f <(printf '%s\n' "$labels") "$impacted" || [ $? -eq 1 ]; } | sort -u > "$out"
  fi
}
