#!/usr/bin/env bash
# Rebuilds, from git clones, the Bazel modules whose source archives a
# download-blocking proxy refuses.
#
# Adapted from smithy-cpp's bazel/make-git-overrides.sh (see its
# docs/development.md, "Sandboxed sessions"). The approach is theirs. Three
# things MoonBase's module graph needs that theirs does not, each marked at
# the site: commit-pinned modules (their clone uses --branch, which only
# takes refs), a strip_prefix that points into a subdirectory rather than at
# the archive root, and retrying fetches.
#
# The proxy in cloud dev sandboxes and some CI runners allows git-over-HTTPS
# and GitHub *release assets* (/releases/download/...) but 403s the on-demand
# source-archive endpoints (/archive/refs/tags/..., codeload.github.com) that
# most BCR modules use as their source URL. In this repo that blocks libpng,
# every modular Boost archive (so Beast, so every beast_transport target),
# cel-spec (so gazelle, so `bazel run //:buildifier`, so scripts/format-all),
# and opentelemetry-cpp — i.e. most of the graphics and platform domains.
#
# Only the archive-URL modules need help: this finds them in
# MODULE.bazel.lock, clones each at its pinned tag, replays the BCR's patches
# and overlay files from bcr.bazel.build (registry metadata is not blocked),
# and emits an `--override_module` line per module into
# <dest>/overrides.bazelrc.
#
# Usage:   scripts/make-git-overrides.sh [dest-dir]   # default ~/bazel-overrides
# Then:    cat >> .bazelrc.user <<RC
#          common --lockfile_mode=off
#          import $HOME/bazel-overrides/overrides.bazelrc
#          RC
#
# The heredoc is unquoted on purpose: a bazelrc `import` takes a literal path
# and expands neither ~ nor environment variables, so $HOME has to be
# expanded as the file is written.
#
# --lockfile_mode=off is required: overridden modules drop out of lockfile
# verification, and without it every run dirties the checked-in
# MODULE.bazel.lock. Re-run after a dep bump; it is idempotent and skips
# modules already built.
#
# This covers BCR *modules*. Two repos in this build are fetched by module
# extensions rather than the registry and so never appear in the scan —
# container_structure_test and aspect_bazel_lib's bats toolchain; those keep
# their --override_repository treatment in scripts/bazel_restricted_egress.sh,
# which is the wrapper to use once this script has run.
set -euo pipefail

DEST="${1:-$HOME/bazel-overrides}"
REGISTRY="https://bcr.bazel.build"
LOCKFILE="$(dirname "$0")/../MODULE.bazel.lock"
RC="$DEST/overrides.bazelrc"
mkdir -p "$DEST"
: > "$RC.tmp"

command -v jq >/dev/null || { echo "jq is required" >&2; exit 1; }

# The proxy that makes this script necessary also resets connections under
# load, and one dropped fetch part-way through a 40-module run wastes the
# whole run. Retry rather than restart.
fetch() { curl -fsS --retry 4 --retry-all-errors --retry-delay 2 "$@"; }

# Modules that toolchain registration fetches during analysis without the
# lockfile ever recording a source.json for them (rules_perl arrives via
# the openssl module's toolchains). The lockfile scan below cannot see
# these, so they are pinned here; if a build still 403s on a module this
# script didn't cover, add it and re-run.
EXTRA_MODULES="rules_perl/0.5.0"

# Every module version the lockfile consulted a source.json for, plus the
# extras above.
modules=$(
  { grep -oE '"https://bcr\.bazel\.build/modules/[^"]+/source\.json"' "$LOCKFILE" |
      sed 's|.*/modules/||; s|/source\.json"||'
    printf '%s\n' $EXTRA_MODULES; } | sort -u)

for mod in $modules; do
  name="${mod%%/*}" version="${mod#*/}"
  src="$(fetch "$REGISTRY/modules/$mod/source.json")"
  url="$(jq -r .url <<<"$src")"
  # Release-asset and non-GitHub URLs download fine; only the on-demand
  # archive endpoints are blocked.
  case "$url" in
    *github.com/*/archive/*) ;;
    *) continue ;;
  esac
  # A module's root is not always the archive root. strip_prefix's first
  # component is the directory GitHub wraps an archive in ("libpng-1.6.54"),
  # which a clone does not have; any component after that is a real
  # subdirectory of the repo and is where the module actually lives
  # (opencensus-proto is ".../src"). Patches, overlay files, the registry
  # MODULE.bazel, and the override itself all have to land there.
  strip_prefix="$(jq -r '.strip_prefix // ""' <<<"$src")"
  subdir="${strip_prefix#*/}"
  [ "$subdir" = "$strip_prefix" ] && subdir=""

  out="$DEST/$name-$version"
  if [ ! -d "$out" ]; then
    # https://github.com/<org>/<repo>/archive/refs/tags/<tag>.{tar.gz,zip}
    repo="$(sed -E 's|https://github.com/([^/]+/[^/]+)/archive/.*|\1|' <<<"$url")"
    tag="$(sed -E 's|.*/archive/(refs/tags/)?(.*)\.(tar\.gz\|zip)|\2|' <<<"$url")"
    echo ">> $mod  <-  $repo @ $tag"
    if ! git -c advice.detachedHead=false clone -q --depth 1 --branch "$tag" \
        "https://github.com/$repo.git" "$out.tmp" 2>/dev/null; then
      # Not every archive URL names a ref. Some modules pin a raw commit
      # (envoy_api, and anything else the BCR versions as a date+sha), and
      # `clone --branch` only accepts branches and tags. Fetching the object
      # by name works for those, and needs the repo to exist first.
      rm -rf "$out.tmp"
      git -c advice.detachedHead=false init -q "$out.tmp"
      git -C "$out.tmp" remote add origin "https://github.com/$repo.git"
      git -C "$out.tmp" fetch -q --depth 1 origin "$tag"
      git -C "$out.tmp" -c advice.detachedHead=false checkout -q FETCH_HEAD
    fi
    rm -rf "$out.tmp/.git"
    root="$out.tmp${subdir:+/$subdir}"
    # The BCR's patches (patch_strip applies to all of them). --batch so a
    # patch that cannot find its target fails the script instead of blocking
    # on "Skip this patch?" forever in a non-interactive run.
    strip="$(jq -r '.patch_strip // 0' <<<"$src")"
    for p in $(jq -r '(.patches // {}) | keys[]' <<<"$src"); do
      fetch "$REGISTRY/modules/$mod/patches/$p" |
        patch -s --batch -p"$strip" -d "$root"
    done
    # ...then overlay files (BUILD.bazel, ...) land on top...
    for f in $(jq -r '(.overlay // {}) | keys[]' <<<"$src"); do
      mkdir -p "$root/$(dirname "$f")"
      fetch "$REGISTRY/modules/$mod/overlay/$f" -o "$root/$f"
    done
    # ...and the registry's MODULE.bazel is authoritative — for registry
    # modules Bazel injects it over whatever the archive carries (boost
    # upstreams carry none at all), so a local override needs it too.
    fetch "$REGISTRY/modules/$mod/MODULE.bazel" -o "$root/MODULE.bazel"
    mv "$out.tmp" "$out"
  fi
  echo "common --override_module=$name=$out${subdir:+/$subdir}" >> "$RC.tmp"
done

# Two repos come from module extensions rather than the registry, so the
# lockfile scan above cannot see them and --override_module does not apply.
# They need --override_repository against their canonical names, which are
# derived from the extension that declares them and will change if that
# wiring does — if analysis 403s on one of these, re-read the name Bazel
# printed in the error and update it here.

# bats: aspect_bazel_lib registers it as a toolchain, and toolchain
# registration makes *every* analysis fetch it. An empty package rather than
# a clone — --override_repository does not apply the build_file_content the
# extension's http_archive injects, and rebuilding that file by hand does not
# work either, because the module graph holds two instances of
# aspect_bazel_lib under different canonical names and a BUILD written
# against one cannot resolve the other's labels. Nothing in this repo defines
# a bats_test, so the toolchain only has to be registrable, never resolvable.
# Both instances need overriding; whichever is left out is the one that fails.
BATS="$DEST/bats-stub"
mkdir -p "$BATS"
touch "$BATS/REPO.bazel" "$BATS/BUILD.bazel"
for repo in aspect_bazel_lib++toolchains+bats_toolchains bazel_lib++toolchains+bats_toolchains; do
  echo "common --override_repository=$repo=$BATS" >> "$RC.tmp"
done

# raylib: //bazel/extensions:raylib.bzl fetches the archive and supplies the
# BUILD file from this repo (bazel/3p/raylib.BUILD). An override does not
# carry that over, so the clone gets it copied in as its own BUILD.bazel.
RAYLIB="$DEST/raylib-5.5"
if [ ! -d "$RAYLIB" ]; then
  echo ">> raylib/5.5  <-  raysan5/raylib @ 5.5"
  git -c advice.detachedHead=false clone -q --depth 1 --branch 5.5 \
    https://github.com/raysan5/raylib.git "$RAYLIB.tmp"
  rm -rf "$RAYLIB.tmp/.git"
  cp "$(dirname "$0")/../bazel/3p/raylib.BUILD" "$RAYLIB.tmp/BUILD.bazel"
  # --override_repository needs a repo-boundary marker; unlike
  # --override_module there is no MODULE.bazel to serve as one.
  touch "$RAYLIB.tmp/REPO.bazel"
  mv "$RAYLIB.tmp" "$RAYLIB"
fi
echo "common --override_repository=+raylib+raylib=$RAYLIB" >> "$RC.tmp"

mv "$RC.tmp" "$RC"
echo "wrote $RC ($(grep -c override_module "$RC") module overrides + 3 repo overrides)"
