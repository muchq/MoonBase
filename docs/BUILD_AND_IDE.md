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
