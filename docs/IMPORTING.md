### Importing New Repositories
- clone the new repo `foo`
- in `foo`, `mkdir import-foo-dir` and `git mv` everything into that directory to avoid conflicts
- in `MoonBase` do:
    - `gco -b import-foo-project`
    - `git remote add foo ${path-to-foo}`
    - `git fetch foo`
    - `git merge foo/main --allow-unrelated-histories`
    - put stuff where you want it and bazel-ify
