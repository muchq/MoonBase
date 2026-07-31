#!/bin/bash
# Bazel wrapper for environments whose egress policy blocks GitHub source
# archives (https://github.com/<org>/<repo>/archive/...) while still
# allowing git, GitHub release assets, and the Bazel Central Registry.
# Cloud dev sandboxes and CI runners behind a filtering proxy land here.
#
# Two dependencies fail before a single source file compiles, and they
# are worth special-casing because they stop *every* target regardless of
# what you asked to build:
#
#   container_structure_test  //bazel/rules:oci.bzl loads it, so it
#                             breaks loading any package whose BUILD file
#                             uses the OCI rules — including a plain
#                             cc_library sitting in that package.
#   bats-core                 aspect_bazel_lib registers it as a
#                             toolchain, so it breaks analysis no matter
#                             which target you name.
#
# This wrapper clears both. It does not clear everything: see LIMITS.
#
# SUPERSEDED — prefer scripts/make-git-overrides.sh, which covers every
# blocked module rather than these two and gets the whole graph building.
# This script is kept because it needs no setup step, which is convenient
# for a one-off build of a target that only needs the two repos below.
#
# Usage:
#   scripts/bazel_restricted_egress.sh test //domains/games/apis/golf_hub:chat_store_test
#   scripts/bazel_restricted_egress.sh build //domains/games/apis/golf_hub:golf_hub_smithy_server
#
# LIMITS
#
# Individual libraries fetched from the blocked endpoint still fail, and
# each one only surfaces once the previous is resolved, so expect to
# discover them one build at a time. Known blockers in this repo:
#
#   opentelemetry-cpp -> opentelemetry-proto -> ...  (//domains/platform/libs/futility/otel)
#   boost.beast -> boost.*                           (smithy_cpp//runtime:http_beast)
#   libpng                                           (//domains/graphics/libs/png_plusplus)
#   cel-spec -> gazelle                              (//:buildifier, scripts/format-all)
#
# That rules out anything depending on the otel metrics recorder or on
# the Beast websocket transport, most of domains/graphics, and the Bazel
# formatter. Pure C++/Abseil targets and the Smithy codegen targets do
# build, which is enough to typecheck a model change and run store-level
# tests.
#
# All of the above is what scripts/make-git-overrides.sh exists to fix.
# A BCR-overlay library (boost.*, libpng, most non-Bazel C++ libraries)
# cannot be overridden from a *bare* clone, because the overlay is not in
# the clone — but the overlay is served by bcr.bazel.build, which the
# proxy does not block, so fetching it onto the clone works. That is what
# make-git-overrides.sh does, for every such module at once.
#
# On a machine with unrestricted egress this script is unnecessary; run
# bazel directly.
set -euo pipefail

CACHE="${BAZEL_EGRESS_CACHE:-$HOME/.cache/bazel-egress-overrides}"
CST_TAG="v1.22.1"  # keep in sync with bazel/oci.MODULE.bazel

mkdir -p "$CACHE"

# container-structure-test ships its own MODULE.bazel, so a clone works
# as an override directory unmodified.
if [ ! -d "$CACHE/container-structure-test" ]; then
  echo "cloning container-structure-test $CST_TAG..." >&2
  git clone --depth 1 --branch "$CST_TAG" \
    https://github.com/GoogleContainerTools/container-structure-test.git \
    "$CACHE/container-structure-test"
fi

# bats gets an empty package rather than a clone. --override_repository
# does not apply the build_file_content that aspect_bazel_lib's
# http_archive injects, and rebuilding that file by hand does not work
# either: the module graph holds two instances of aspect_bazel_lib under
# different canonical names, and a BUILD written against one cannot
# resolve the other's labels. Nothing in this repo defines a bats_test,
# so the toolchain only has to be registrable, never resolvable — an
# empty package satisfies that and sidesteps the label problem entirely.
if [ ! -d "$CACHE/bats-stub" ]; then
  mkdir -p "$CACHE/bats-stub"
  touch "$CACHE/bats-stub/REPO.bazel" "$CACHE/bats-stub/BUILD.bazel"
fi

# Both aspect_bazel_lib instances need overriding; whichever is left out
# is the one that fails.
exec bazel "$@" \
  --override_repository=container_structure_test="$CACHE/container-structure-test" \
  --override_repository=aspect_bazel_lib++toolchains+bats_toolchains="$CACHE/bats-stub" \
  --override_repository=bazel_lib++toolchains+bats_toolchains="$CACHE/bats-stub" \
  ${BAZEL_EGRESS_EXTRA_OVERRIDES:-}
