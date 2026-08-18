# Build and IDE Support

## Building the Project

This project uses [Bazel](https://bazel.build/) as the primary build system for all languages (C++, Go, Java, Rust, Scala).

### Common Commands

Build all targets:
```bash
bazel build //...
```

Run tests:
```bash
bazel test //...
```

Run a specific target:
```bash
bazel run //path/to/target
```

### Building behind a restrictive egress proxy

Some sandboxes and CI runners sit behind a proxy that allows the Bazel
Central Registry, GitHub release assets, and git, but blocks GitHub
*source archives* — `https://github.com/<org>/<repo>/archive/...` — with
a 403. That is how most BCR modules fetch their sources, so a great deal
of the build stops before anything compiles: every modular Boost archive
(and so every Beast-transport target), `libpng` (and so `png_plusplus`,
portrait, and tracy_demo — not the rest of `domains/graphics`, which never
reaches it), `opentelemetry-cpp`, and `cel-spec` — which gazelle pulls in,
which means even `bazel run //:buildifier` and `scripts/format-all` fail.

Run [`scripts/make-git-overrides.sh`](../scripts/make-git-overrides.sh)
once, then import its output:

```bash
scripts/make-git-overrides.sh          # builds ~/bazel-overrides (idempotent)
cat >> .bazelrc.user <<RC
common --lockfile_mode=off
import $HOME/bazel-overrides/overrides.bazelrc
RC
```

The heredoc is deliberately unquoted so `$HOME` expands as the file is
written: a bazelrc `import` takes a literal path, expanding neither `~`
nor environment variables, so the absolute path has to be baked in. (If
you passed a dest-dir argument to the script, import from there instead —
it prints the path it wrote.)

`.bazelrc.user` is gitignored and `try-import`ed by `.bazelrc`, so this
stays out of version control. After it, `bazel build --nobuild //...`
analyses clean — every fetch resolves, with no exclusion list — and
Beast, libpng, otel, and buildifier targets build and test normally.

The script finds every module in `MODULE.bazel.lock` whose source URL is
a blocked archive endpoint, clones each at its pinned tag (git works
where the archive download does not), replays the BCR's patches, overlay
files, and registry `MODULE.bazel` from `bcr.bazel.build` — registry
metadata is not blocked — and writes one `--override_module` line per
module.

It then covers the two kinds that scan structurally cannot find. Modules
pinned in a `MODULE.bazel` with `archive_override` — `smithy_cpp` — are
not served by the registry, so they have no `source.json` for the scan to
walk; they are read straight out of the `MODULE.bazel` files and cloned
at their pinned commit. And repos created by module extensions are not
modules at all: the `bats` toolchain gets an empty stub, and `raylib`
gets a clone plus this repo's own `bazel/3p/raylib.BUILD`.

`smithy_cpp` is worth knowing about specifically, because it fails
earlier and louder than the rest. Bazel resolves the module graph before
it analyses anything, so an uncovered `archive_override` takes down
*every* bazel command — `version` aside — with a 403 during "Computing
main repo mapping", well before the first target is looked at.

`--lockfile_mode=off` is required: overridden modules drop out of
lockfile verification, and without it every run dirties the checked-in
`MODULE.bazel.lock`. Re-run the script after a dep bump; version numbers
are read from the lockfile and the extension files, never hardcoded, so a
re-run picks a bump up on its own.

If a build still 403s on something the script didn't cover, diagnose
before pinning. **Do not add a module override by hand without checking
what the graph actually resolved** — `--override_module` forces the
*version*, so pinning the one named in a 403 can silently downgrade a
module that something else depends on a newer API of. `bazel mod
show_repo <name>` tells you the resolved version, and a BCR source URL
under `/releases/download/` is a release asset the proxy allows, meaning
the module needs no override at all. (This is not hypothetical: an
inherited `EXTRA_MODULES="rules_perl/0.5.0"` line downgraded a
lockfile-resolved 1.1.0 and broke `@openssl` loading, taking every `bazel
query` that reached it down with it.) A repo created by a module
extension is the other case — it needs `--override_repository` against
the canonical name from Bazel's error, following the `bats`/`raylib`
blocks at the bottom of the script. A module added with a *new*
`archive_override` needs nothing: the script reads those out of the
`MODULE.bazel` files on every run, so a bumped commit is picked up the
same way a bumped lockfile version is.

Known residual: `bazel query //...` additionally pulls rules_apple's iOS
test runner (`xctestrunner`), which is still blocked. `bazel build
--nobuild //...` does not, and the iOS targets only build on macOS
anyway — so scope a `query` universe below `//...` on Linux.

The approach is lifted from smithy-cpp's `bazel/make-git-overrides.sh`
(its `docs/development.md`, "Sandboxed sessions").

On a machine with normal egress, ignore all of this and run `bazel`.

## IDE Support

Two different JetBrains plugins are in play, and which one you get depends on
the IDE. They are not interchangeable, and the split is the reason C++ work
does not belong in IDEA.

### IntelliJ IDEA — the Bazel plugin, JVM/Go/Python only

[Bazel](https://plugins.jetbrains.com/plugin/22977-bazel) (`org.jetbrains.bazel`).
It generates its project view at `.bazelbsp/.bazelproject`, seeded from
[`bazel/intellij/universe.bazelproject`](../bazel/intellij/universe.bazelproject);
add targets there if sources aren't detected automatically.

**This plugin ships no C++ module in the IDEA distribution.** Its language
modules are Java, Kotlin, Go, Python, and protobuf — there is no `cpp`, `clion`,
or `cidr` module in `bazel-plugin/lib/modules/`. So no C++ target gets a project
model in IDEA, and there is no setting that adds one; the C/C++ engine IDEA
bundles logs `Entry points unavailable` for every `.cc` file in the repo.

The failure is easy to misread, because IDEA still resolves *some* includes.
With no model, includes are matched against paths under the content root, so
`#include "domains/games/apis/golf_hub/hub_store.h"` finds a real file and looks
fine, while anything whose search root only exists in `bazel-out` does not.
Generated code is the whole of that second category — `#include
"moonbase/golf/server.h"` resolves only via the `includes` attribute on the
`cc_library` that wraps the Smithy codegen. The result reads as "generated code
is invisible" when the truth is that no C++ model exists at all.

### CLion — clwb, for C++

[Bazel for IntelliJ](https://plugins.jetbrains.com/plugin/8609-bazel-for-intellij)
(`com.google.idea.bazel.clwb`). This is the plugin with the C++ aspect, and it
is where C++ work in this repo belongs.

Point the plugin's generated `.clwb/.bazelproject` at the version-controlled
view and re-sync:

```
import bazel/intellij/clion.bazelproject
```

Generated sources need no extra configuration. The aspect reads
`CcInfo.compilation_context`, so an `includes` attribute on the generating
`cc_library` becomes a header search root in the IDE; sync materializes the
generated headers themselves. `golf_hub` is the worked example — the
`smithy_cpp_server_library` macro sets `includes = ["<name>_smithy_gen/include"]`,
which is what makes `#include "moonbase/golf/server.h"` resolve.

**`.bazelignore` must not list `.clwb`.** The plugin writes its aspect to
`.clwb/aspects/legacy/` and then passes `--aspects=//.clwb/aspects/legacy:...`.
Ignoring the directory makes that label unloadable and sync dies with `'.clwb/
aspects/legacy' is not a package`. Unignoring it costs nothing: the aspect's
`BUILD` file is empty, so `bazel query //...` returns the same target set either
way. `.ijwb` stays ignored — its `aspects/BUILD.bazel` calls `define_flag_hack()`
and would inject real targets.

Rust is not covered. clwb has no Rust language class, and the IntelliJ Rust
plugin does not read Bazel — Rust sources open and highlight in CLion, but with
no target model behind them.

### VSCode

For C++, [hedronvision/bazel-compile-commands-extractor](https://github.com/hedronvision/bazel-compile-commands-extractor)
gives clangd a compilation database. **It is not currently wired up** —
`@hedron_compile_commands` is not in `MODULE.bazel`, so the `refresh_all` target
does not exist and would have to be added first. The `compile_commands.json` at
the repo root is a stale leftover from before the `domains/` reorg and describes
a source layout that no longer exists.

Note that a compilation database buys nothing in IntelliJ IDEA: nothing there
consumes it. It is useful to clangd (VSCode, or an LSP client) and to CLion.

## Importing a project?
See [IMPORTING.md](./IMPORTING.md) for details on adding new projects to the repository.
