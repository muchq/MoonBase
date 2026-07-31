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
(and so every Beast-transport target), `libpng` (and so all of
`domains/graphics`), `opentelemetry-cpp`, and `cel-spec` — which gazelle
pulls in, which means even `bazel run //:buildifier` and
`scripts/format-all` fail.

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
module. It then handles the two repos that come from module extensions
rather than the registry, and so never appear in that scan: the `bats`
toolchain gets an empty stub, and `raylib` gets a clone plus this repo's
own `bazel/3p/raylib.BUILD`.

`--lockfile_mode=off` is required: overridden modules drop out of
lockfile verification, and without it every run dirties the checked-in
`MODULE.bazel.lock`. Re-run the script after a dep bump.

If a build still 403s on something the script didn't cover, which of the
two lists it belongs in depends on where it came from. A registry module
that toolchain registration fetches without the lockfile recording it
goes in `EXTRA_MODULES`. A repo created by a module extension needs an
`--override_repository` against its canonical name — take that name from
the error Bazel printed, and follow the `bats`/`raylib` cases at the
bottom of the script.

The approach is lifted from smithy-cpp's `bazel/make-git-overrides.sh`
(its `docs/development.md`, "Sandboxed sessions").
[`scripts/bazel_restricted_egress.sh`](../scripts/bazel_restricted_egress.sh)
predates it and covers a strict subset; prefer this.

On a machine with normal egress, ignore all of this and run `bazel`.

## IDE Support

### IntelliJ IDEA
Tested with the [Bazel for IntelliJ](https://plugins.jetbrains.com/plugin/8609-bazel-for-intellij) plugin.

Java, Go, and Python targets are well-supported. Add new targets to the [project view](bazel/intellij/universe.bazelproject) if they aren't detected automatically.

### CLion
C++ and Rust projects work with the [Bazel for IntelliJ](https://plugins.jetbrains.com/plugin/8609-bazel-for-intellij) plugin.

### VSCode
For C++, [hedronvision/bazel-compile-commands-extractor](https://github.com/hedronvision/bazel-compile-commands-extractor) is recommended for better IntelliSense.

Follow the instructions [here](https://github.com/hedronvision/bazel-compile-commands-extractor#vscode), then run:

```bash
bazel run @hedron_compile_commands//:refresh_all
code .
```

## Importing a project?
See [IMPORTING.md](./IMPORTING.md) for details on adding new projects to the repository.
