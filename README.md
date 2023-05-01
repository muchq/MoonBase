# MoonBase

[![CircleCI](https://dl.circleci.com/status-badge/img/gh/muchq/MoonBase/tree/main.svg?style=svg)](https://dl.circleci.com/status-badge/redirect/gh/muchq/MoonBase/tree/main)

![MoonBase](moon.gif)

### IntelliJ
Tested with [Bazel for IntelliJ](https://plugins.jetbrains.com/plugin/8609-bazel-for-intellij)

Java targets Just Work™.

Add new targets to [project view](/.ijwb/.bazelproject) if they aren't detected automatically.

### Dependencies
 - update `dependencies.yaml`
 - run `./scripts/update_deps.sh`

### Importing New Repositories
 - clone the new repo `foo`
 - in `foo`, `mkdir import-foo-dir` and `git mv` everything into that directory to avoid conflicts
 - in `MoonBase` do:
   - `gco -b import-foo-project`
   - `git remote add foo ${path-to-foo}`
   - `git fetch foo`
   - `git merge foo/main --allow-unrelated-histories`
   - put stuff where you want it and bazel-ify
