# Shared by scripts/diff-build (CI) and scripts/diff-build-decide (test-only).
# Sourced, not executed. Keep this free of side effects beyond defining the
# pathspec list and the two helpers below.

# Paths whose change makes bazel-diff's impacted-target set untrustworthy for
# the property CI needs: every consumer that would feel the change actually
# builds. Config files (.bazelversion, .bazelrc) are the original case. Shared
# macros under bazel/rules/ are the same class — a java.bzl / oci.bzl edit
# reshapes every target that loads those macros, and trusting an impact set
# that may only name //bazel/rules:* is how analysis failures reach main
# (#1378, found on a NullAway change but not Java-specific).
#
# Scoped to the macros and their package BUILD, not the whole directory:
# testdata/ and java_image_test.yaml only feed //bazel/rules:rules_test.
FULL_BUILD_PATHSPECS=(
  .bazelversion
  .bazelrc
  'bazel/rules/*.bzl'
  bazel/rules/BUILD.bazel
  # Dependency pins: bazel-diff is not run with external-repo hashing, so an
  # archive_override bump would otherwise miss every consumer.
  MODULE.bazel
  'bazel/*.MODULE.bazel'
)

# Fail loudly when BASE_REVISION does not resolve. An unfetched origin/main,
# shallow fork checkout, or gc'd sha must not look like "nothing changed"
# and silently take the bazel-diff path (#1378 review finding 1).
require_base_revision() {
  local base=$1
  if ! git rev-parse --verify "${base}^{commit}" >/dev/null 2>&1; then
    echo "ERROR: base revision does not resolve: ${base}" >&2
    echo "Fetch it (e.g. git fetch origin main) before running diff-build." >&2
    return 1
  fi
}

# Paths between base and HEAD that force a full //... build, one per line, or
# empty. Caller must have already required the base; git diff failures propagate.
paths_forcing_full_build() {
  local base=$1
  git diff --name-only "$base" HEAD -- "${FULL_BUILD_PATHSPECS[@]}"
}
