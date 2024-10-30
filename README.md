# MoonBase

![Test + Publish](https://github.com/muchq/MoonBase/actions/workflows/publish.yml/badge.svg)

![MoonBase](static_content/moon.gif)

### IntelliJ
Tested with [Bazel for IntelliJ](https://plugins.jetbrains.com/plugin/8609-bazel-for-intellij)

Java and Go targets Just Work™.

Add new targets to [project view](/.ijwb/.bazelproject) if they aren't detected automatically.

### Clion
C++ and Rust projects work with [Bazel for IntelliJ](https://plugins.jetbrains.com/plugin/8609-bazel-for-intellij), but autocomplete/intellisense doesn't feel very snappy.

### GoLand
Go projects work with [Bazel for IntelliJ](https://plugins.jetbrains.com/plugin/8609-bazel-for-intellij).
Alternatively, you can use IntelliJ for Go too.

### VSCode

For C++ use [hedronvision/bazel-compile-commands-extractor](https://github.com/hedronvision/bazel-compile-commands-extractor)

Follow instructions [here](https://github.com/hedronvision/bazel-compile-commands-extractor#vscode)

and then do
```
bazel run @hedron_compile_commands//:refresh_all
code .
```

### Importing a project?
See [IMPORTING](./IMPORTING.md)
