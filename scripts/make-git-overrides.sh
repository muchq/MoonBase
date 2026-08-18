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
# Registry modules are the bulk of it and are found by scanning the
# lockfile. Two other kinds cannot be found that way and are handled after
# it: modules pinned with archive_override, which have no source.json for
# the scan to walk (smithy_cpp), and repos created by module extensions,
# which are not modules at all (the bats toolchain, raylib).
# (container_structure_test needs no special case — it is a registry
# module, so the scan already covers it.)
set -euo pipefail

DEST="${1:-$HOME/bazel-overrides}"
REGISTRY="https://bcr.bazel.build"
LOCKFILE="$(dirname "$0")/../MODULE.bazel.lock"
RC="$DEST/overrides.bazelrc"
mkdir -p "$DEST"
: > "$RC.tmp"

command -v jq >/dev/null || { echo "jq is required" >&2; exit 1; }

# Enumerating archive_override pins and deciding where their clones land is
# pure text handling, so it lives in a sourceable lib that
# scripts/test-make-git-overrides can exercise offline.
# shellcheck source=scripts/make-git-overrides-lib.sh
. "$(dirname "$0")/make-git-overrides-lib.sh"

# The proxy that makes this script necessary also resets connections under
# load, and one dropped fetch part-way through a 40-module run wastes the
# whole run. Retry rather than restart.
fetch() { curl -fsS --retry 4 --retry-all-errors --retry-delay 2 "$@"; }

# Every module version the lockfile consulted a source.json for.
#
# No hand-maintained extras list. smithy-cpp's version carries
# EXTRA_MODULES="rules_perl/0.5.0" for a module its lockfile does not
# record; ours does record rules_perl, at 1.1.0, whose BCR source is a
# release asset the proxy allows — so the scan rightly skips it and no
# override is wanted. Adding one is actively harmful: --override_module
# forces the *version*, so pinning 0.5.0 downgrades the graph under
# @openssl (reached via curl, boost.asio, and postgres), which then fails
# to load with "no such attribute 'perlopt' in 'perl_binary' rule" and
# takes every `bazel query` touching it down with it.
#
# So: if a build 403s on a module this scan missed, find out *why* it is
# missing before pinning anything. A version written here overrides
# whatever MVS resolved, and `bazel mod show_repo <name>` is how you check
# what that was.
modules=$(
  grep -oE '"https://bcr\.bazel\.build/modules/[^"]+/source\.json"' "$LOCKFILE" |
    sed 's|.*/modules/||; s|/source\.json"||' | sort -u)

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

# Modules this repo pins with archive_override rather than taking from the
# registry (#1349). They are invisible to the scan above for a structural
# reason: it walks source.json URLs, and a module the registry does not
# serve has no source.json to record — so smithy_cpp never appeared, and
# every bazel command in a sandbox died before analysis with a 403 on its
# archive URL. Not "the scan missed one": it could not have seen it.
#
# Read out of the MODULE.bazel files rather than listed here, for the same
# reason the raylib tag below is: a hardcoded pin next to a "keep in sync"
# comment fails silently the first time somebody bumps the commit, and
# --override_module wins, so the stale clone is quietly what gets built.
#
# Nothing to fetch from the registry for these, unlike the loop above — an
# archive_override module carries its own MODULE.bazel, which is most of
# why it is overridden rather than published.
MODULE_FILES=("$(dirname "$0")/../MODULE.bazel" "$(dirname "$0")"/../bazel/*.MODULE.bazel)
while IFS=$'\t' read -r name url strip_prefix; do
  [ -n "$name" ] || continue
  is_blocked_archive_url "$url" || continue

  repo="$(override_repo "$url")"
  ref="$(override_ref "$url")"
  subdir="$(override_subdir "$strip_prefix")"
  out="$(override_dir "$DEST" "$name" "$ref")"

  if [ ! -d "$out" ]; then
    echo ">> $name (archive_override)  <-  $repo @ $ref"
    # Commit-pinned far more often than tagged: a commit is the only thing
    # you can pin before a project cuts releases, which is a large part of
    # why it is not in the registry. So fetch the object by name first, and
    # keep --branch as the fallback for the tag case.
    if ! (git -c advice.detachedHead=false init -q "$out.tmp" &&
      git -C "$out.tmp" remote add origin "https://github.com/$repo.git" &&
      git -C "$out.tmp" fetch -q --depth 1 origin "$ref" &&
      git -C "$out.tmp" -c advice.detachedHead=false checkout -q FETCH_HEAD); then
      rm -rf "$out.tmp"
      git -c advice.detachedHead=false clone -q --depth 1 --branch "$ref" \
        "https://github.com/$repo.git" "$out.tmp"
    fi
    rm -rf "$out.tmp/.git"
    mv "$out.tmp" "$out"
  fi
  echo "common --override_module=$name=$out${subdir:+/$subdir}" >> "$RC.tmp"
done < <(archive_override_pins "${MODULE_FILES[@]}")

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
#
# The tag is read out of the extension rather than written here. A hardcoded
# pin plus a "keep in sync" comment is what the old wrapper did, and it fails
# silently in the worst direction: --override_repository wins, so a bumped
# raylib would keep building the stale sources against the new BUILD file
# with nothing reporting a mismatch.
RAYLIB_BZL="$(dirname "$0")/../bazel/extensions/raylib.bzl"
RAYLIB_TAG="$(grep -oE 'raylib/archive/refs/tags/[^"]+' "$RAYLIB_BZL" | head -1)"
RAYLIB_TAG="${RAYLIB_TAG##*/}"
RAYLIB_TAG="${RAYLIB_TAG%.zip}"
[ -n "$RAYLIB_TAG" ] || { echo "could not read the raylib tag from $RAYLIB_BZL" >&2; exit 1; }
RAYLIB="$DEST/raylib-$RAYLIB_TAG"
if [ ! -d "$RAYLIB" ]; then
  echo ">> raylib/$RAYLIB_TAG  <-  raysan5/raylib @ $RAYLIB_TAG"
  git -c advice.detachedHead=false clone -q --depth 1 --branch "$RAYLIB_TAG" \
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
