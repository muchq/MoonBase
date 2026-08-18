# Shared by scripts/make-git-overrides (the real thing) and
# scripts/test-make-git-overrides. Sourced, not executed: everything here is
# pure text, so the test can exercise it without the network the rest of that
# script needs.
#
# The split exists because the part worth testing and the part that needs the
# network are different parts. Enumerating archive_override pins and deciding
# where each clone lands is string handling over MODULE.bazel files; only the
# clone itself talks to GitHub. Keeping the first half sourceable is what lets
# a CI test assert the property that a bumped pin picks a new directory —
# which is the property that was wrong when this path first landed (#1392
# review): the output was keyed "$name-override", so a re-run after a bump
# found the old clone, skipped the fetch, and emitted an override that quietly
# built the previous commit.

# Emits one "name<TAB>url<TAB>strip_prefix" line per archive_override found in
# the given MODULE.bazel files.
#
# A hand-rolled reader rather than bazel's own: this runs *because* bazel
# cannot, and the whole point is to unblock the first `bazel` invocation.
archive_override_pins() {
  awk '
    # The first double-quoted string on the line, unquoted. Deliberately not
    # a gsub of the surrounding text: /.*"/ is greedy and eats through the
    # closing quote, which silently yields an empty field rather than a
    # wrong one, and an empty field here reads exactly like "this repo pins
    # nothing with archive_override".
    function quoted(line,   value) {
      if (!match(line, /"[^"]*"/)) return ""
      return substr(line, RSTART + 1, RLENGTH - 2)
    }
    /archive_override\(/ { in_block = 1; name = ""; url = ""; prefix = ""; next }
    in_block && /^\)/ {
      if (name != "" && url != "") print name "\t" url "\t" prefix
      in_block = 0
      next
    }
    in_block {
      if ($0 ~ /module_name[[:space:]]*=/) name = quoted($0)
      if ($0 ~ /strip_prefix[[:space:]]*=/) prefix = quoted($0)
      if (url == "" && $0 ~ /"https:\/\//) url = quoted($0)
    }
  ' "$@"
}

# True when a URL points at the on-demand source-archive endpoint the proxy
# blocks. Release assets and non-GitHub URLs download fine and need no
# override.
is_blocked_archive_url() {
  case "$1" in
    *github.com/*/archive/*) return 0 ;;
    *) return 1 ;;
  esac
}

# owner/repo out of a GitHub archive URL.
override_repo() {
  sed -E 's|https://github.com/([^/]+/[^/]+)/archive/.*|\1|' <<<"$1"
}

# The tag or commit an archive URL names.
override_ref() {
  sed -E 's|.*/archive/(refs/tags/)?(.*)\.(tar\.gz\|zip)|\2|' <<<"$1"
}

# The module's root inside the clone. strip_prefix's first component is the
# directory GitHub wraps an archive in, which a clone does not have; anything
# after it is a real subdirectory of the repo.
override_subdir() {
  local strip_prefix=$1 subdir="${1#*/}"
  [ "$subdir" = "$strip_prefix" ] && subdir=""
  printf '%s' "$subdir"
}

# Where a pin's clone lives. The ref is in the name, exactly as the registry
# loop puts the version in its own — see the header above for what a key that
# ignores the ref costs.
override_dir() {
  printf '%s/%s-%s' "$1" "$2" "$3"
}
