# MoonBase

[![CircleCI](https://dl.circleci.com/status-badge/img/gh/muchq/MoonBase/tree/main.svg?style=svg)](https://dl.circleci.com/status-badge/redirect/gh/muchq/MoonBase/tree/main)

![MoonBase](static_content/moon.gif)

[![CircleCI](https://dl.circleci.com/insights-snapshot/gh/muchq/MoonBase/main/moon-base/badge.svg?window=30d)](https://app.circleci.com/insights/github/muchq/MoonBase/workflows/moon-base/overview?branch=main&reporting-window=last-30-days&insights-snapshot=true)

### IntelliJ
Tested with [Bazel for IntelliJ](https://plugins.jetbrains.com/plugin/8609-bazel-for-intellij)

Java, Scala and Go targets Just Work™.

Add new targets to [project view](/.ijwb/.bazelproject) if they aren't detected automatically.

C and C++ aren't supported in Bazel for IntelliJ. Use Clion or VSCode.

### Clion
C++ projects work with [Bazel for IntelliJ](https://plugins.jetbrains.com/plugin/8609-bazel-for-intellij), but autocomplete/intellisense doesn't feel very snappy.

Although it's slow, the clang-lint integration is very helpful.

Not sure why!

### VSCode

Scala3 support in VSCode is waiting on [metals integration](https://github.com/scalameta/metals/issues/5138)

C/C++ use [hedronvision/bazel-compile-commands-extractor](https://github.com/hedronvision/bazel-compile-commands-extractor)

Follow instructions [here](https://github.com/hedronvision/bazel-compile-commands-extractor#vscode)

and then do
```
bazel run @hedron_compile_commands//:refresh_all
code .
```

### JVM Dependencies
Use rules_jvm_external and update deps in WORKSPACE

At some point, switch back to bazel-deps once it [supports scala3](https://github.com/bazeltools/bazel-deps/issues/326)

### Importing New Repositories
 - clone the new repo `foo`
 - in `foo`, `mkdir import-foo-dir` and `git mv` everything into that directory to avoid conflicts
 - in `MoonBase` do:
   - `gco -b import-foo-project`
   - `git remote add foo ${path-to-foo}`
   - `git fetch foo`
   - `git merge foo/main --allow-unrelated-histories`
   - put stuff where you want it and bazel-ify
