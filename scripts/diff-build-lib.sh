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

# Service names from `bazel query 'kind(oci_push, ...)' --output=build` on
# stdin: the last path segment of each rule's repository attribute, sorted and
# unique. This is the parse publish.yml does to tag images, and the names are
# the ones compose.yaml pins and deploy.sh --services lists.
#
# Derived from the dependency graph rather than from changed paths on purpose:
# a shared library fans out to every image that links it, and only the graph
# knows which those are.
services_from_push_rules() {
  sed -n 's/.*repository = "ghcr\.io\/muchq\/\([^"]*\)".*/\1/p' | sort -u
}

# Labels a PR carries for its impacted services. One label per service, all
# under one prefix, so the sync below can retire a stale one without touching
# the hand-applied labels beside it.
SERVICE_LABEL_PREFIX="service:"

# The label changes that bring a PR from the labels it has to the services it
# impacts. Reads two files of names, one per line: the impacted services and
# the labels currently on the PR. Prints "add <label>" and "remove <label>",
# one per line, and nothing for a label already in the right state.
service_label_plan() { # service_label_plan <services-file> <labels-file>
  local services=$1 labels=$2
  local want have
  want=$(sed "s/^/$SERVICE_LABEL_PREFIX/" "$services" | sort -u)
  have=$(grep "^$SERVICE_LABEL_PREFIX" "$labels" | sort -u || true)
  comm -23 <(printf '%s\n' "$want" | grep . || true) <(printf '%s\n' "$have" | grep . || true) \
    | sed 's/^/add /'
  comm -13 <(printf '%s\n' "$want" | grep . || true) <(printf '%s\n' "$have" | grep . || true) \
    | sed 's/^/remove /'
}

# "<service> <image label>" per push rule from `bazel query 'kind(oci_push,
# ...)' --output=build` on stdin: the repository's last path segment and the
# image attribute. What the digest comparison needs — the built image to read
# and the registry repository to read it against — in one line per service.
push_rule_pairs() {
  awk '
    /^oci_push\(/ { svc = ""; img = "" }
    /^ *image = "/ { img = $0; sub(/^ *image = "/, "", img); sub(/".*/, "", img) }
    /^ *repository = "ghcr\.io\/muchq\// {
      svc = $0; sub(/.*repository = "ghcr\.io\/muchq\//, "", svc); sub(/".*/, "", svc)
    }
    /^\)/ && svc != "" && img != "" { print svc, img }
  ' | sort -u
}

# The content digests an image manifest names, one per line in manifest
# order: the config, then each layer. Two manifests with the same list are
# the same image even when their own digests differ — the docker re-tag in
# publish.yml relabels a layer's media type, which changes the manifest's
# digest and nothing the image is made of.
manifest_digests() {
  grep -o '"digest": *"sha256:[a-f0-9]*"' | sed 's/.*"sha256:/sha256:/; s/"$//' || true
}
