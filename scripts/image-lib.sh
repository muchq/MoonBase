# Reading a service's published image: what it is made of, and whether the
# registry has one at a given ref. Sourced by scripts/impacted-services (CI's
# blast-radius comparison) and deploy/consolidated/deploy.sh (the per-service
# history), so the two read a commit's image the same way. No side effects
# beyond definitions and the token cache below.

# The content digests an image manifest names, one per line in manifest
# order: the config, then each layer. Two manifests with the same list are
# the same image even when their own digests differ: commit tags pushed
# before publish tagged from the built image went through docker, which
# relabels a layer's media type and so the manifest's digest, and nothing the
# image is made of.
manifest_digests() {
  grep -o '"digest": *"sha256:[a-f0-9]*"' | sed 's/.*"sha256:/sha256:/; s/"$//' || true
}

REGISTRY_CURL=(curl -sS --connect-timeout 10 --max-time 60 --retry 3)

# One anonymous pull token per repository, minted on first use and again
# after a 401: a listing reads one repository a hundred times and should
# not pay a token round trip for each. The images are public.
REGISTRY_TOKEN=""
REGISTRY_TOKEN_REPO=""
registry_token() { # registry_token <registry url> <repository path>
  if [ "$REGISTRY_TOKEN_REPO" != "$2" ] || [ -z "$REGISTRY_TOKEN" ]; then
    REGISTRY_TOKEN=$("${REGISTRY_CURL[@]}" "$1/token?scope=repository:$2:pull" |
      sed -n 's/.*"token": *"\([^"]*\)".*/\1/p')
    REGISTRY_TOKEN_REPO=$2
  fi
  if [ -z "$REGISTRY_TOKEN" ]; then
    echo "no pull token for $2: the registry refused, or has no such repository" >&2
    return 1
  fi
  printf '%s' "$REGISTRY_TOKEN"
}

# The content digests of the image at <ref> in <repository path>, on stdout.
# Returns 0 with them, 44 when the registry has no such ref, and 1 for
# anything else, said on stderr. Only a manifest is asked for — an index
# names manifests, not content, and the images are pushed one platform each;
# one served anyway is an error rather than a wrong answer.
registry_image_content() { # registry_image_content <registry url> <repository path> <ref>
  local registry=$1 repo=$2 ref=$3 token status body attempt
  body=$(mktemp)
  for attempt in 1 2; do
    token=$(registry_token "$registry" "$repo") || { rm -f "$body"; return 1; }
    status=$("${REGISTRY_CURL[@]}" -o "$body" -w '%{http_code}' \
      -H "Authorization: Bearer $token" \
      -H "Accept: application/vnd.docker.distribution.manifest.v2+json, application/vnd.oci.image.manifest.v1+json" \
      "$registry/v2/$repo/manifests/$ref")
    [ "$status" = 401 ] || break
    REGISTRY_TOKEN=""
  done
  case "$status" in
    200)
      if grep -q '"manifests"' "$body"; then
        echo "registry served an index for $repo:$ref" >&2
        rm -f "$body"
        return 1
      fi
      manifest_digests < "$body"
      rm -f "$body"
      ;;
    404)
      rm -f "$body"
      return 44
      ;;
    *)
      echo "registry returned $status for $repo:$ref" >&2
      rm -f "$body"
      return 1
      ;;
  esac
}
