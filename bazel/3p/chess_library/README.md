# chess-library (vendored)

[Disservin/chess-library](https://github.com/Disservin/chess-library) — a
single-header C++17 chess library: move generation, SAN/UCI/FEN, a streaming
PGN reader, and magic-bitboard attack tables. MIT.

| | |
|---|---|
| Version | 0.9.4 |
| Commit | `53e6a841dcda7059a2af363d85f785ef1817304a` |
| Files | `chess.hpp` (upstream `include/chess.hpp`), `LICENSE` |
| Consumer | `//domains/games/libs/chess_cpp` (the only one — see the BUILD file) |

## Why this is vendored rather than fetched

Every other third-party C++ dependency here comes through bzlmod, and this
one wanted to. It is not in the Bazel Central Registry, which would make it
an `http_archive` in a module extension like `//bazel/extensions:sdl3` —
except that upstream publishes **no releases and no tags**, so the only
fetchable URL is a GitHub source archive.

That is the one endpoint class the proxy in cloud sandboxes and some CI
runners answers with a 403 (see `docs/BUILD_AND_IDE.md`), and
`scripts/make-git-overrides.sh` cannot paper over it here: that script
rebuilds modules it finds in `MODULE.bazel.lock`, and a repository created
by a module extension is not one of them (#1349 is the same shape of miss).
So the choice was a dependency that cannot be fetched in a sandbox, or a
file in the tree.

For one MIT-licensed header this is a good trade: the build is hermetic,
the sandbox story disappears, and the version is a line in this file rather
than a lockfile entry.

## Updating

```bash
SHA=<new commit>
cd bazel/3p/chess_library
curl -sSLO "https://raw.githubusercontent.com/Disservin/chess-library/$SHA/include/chess.hpp"
curl -sSLO "https://raw.githubusercontent.com/Disservin/chess-library/$SHA/LICENSE"
# then update the table above, and run:
bazel test //domains/games/libs/chess_cpp/...
```

`chess_library_contract_test` is the reason that last step is enough to
have an opinion. It pins the upstream behaviors this repo relies on — SAN
rejecting illegal moves, `givesCheck` separating direct from discovered
checks, the PGN reader skipping variations and never emitting the result
token, `unmakeMove` restoring castling rights and the en passant square —
one named test each. An update that changes any of them fails there, by
name, instead of surfacing as a corpus diff.

The header is unmodified. Keep it that way: local edits would have to be
reapplied on every update, and the contract test cannot tell "upstream
changed" from "our patch was lost".
