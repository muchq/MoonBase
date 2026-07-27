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
a 403. Two transitive dependencies fetch that way, and each one stops the
build before anything compiles:

- `container_structure_test`, loaded by `//bazel/rules:oci.bzl`, so it
  breaks loading any package whose `BUILD` file uses the OCI rules — even
  for an unrelated `cc_library` in that package.
- `bats-core`, registered as a toolchain by `aspect_bazel_lib`, so it
  breaks analysis regardless of which target you asked for.

Neither is reachable another way, but both clone fine over git, so
[`scripts/bazel_restricted_egress.sh`](../scripts/bazel_restricted_egress.sh)
points Bazel at local substitutes and passes everything else through:

```bash
scripts/bazel_restricted_egress.sh test //domains/games/apis/golf_hub:chat_store_test
```

It changes nothing in the repo and uses no network path the proxy
refused.

This gets you from "nothing builds" to "most things build", not to a
green `//...`. Individual libraries fetched from the blocked endpoint
still fail, and each surfaces only once the previous one is resolved:
`opentelemetry-cpp` (and then `opentelemetry-proto`) for anything using
the otel metrics recorder, and `boost.beast` for anything using the
smithy-cpp Beast transport. In practice that rules out the service
handlers and the streaming e2e tests, while pure C++/Abseil targets and
the Smithy codegen targets build and test normally — enough to typecheck
a `.smithy` change and run store-level tests.

The script's header documents how to add an override for a blocked
library whose upstream repository is Bazel-native. Libraries that get
their `BUILD` files from a Bazel Central Registry overlay (`boost.*` and
most non-Bazel C++ libraries) cannot be overridden from a bare clone.

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
