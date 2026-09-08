# Deploy

Deploy metadata for MoonBase services

## Service labels on PRs

CI labels every PR with `service:<name>` for each image the change reaches —
`service:mithril`, `service:stats`, and so on — using the same names
`deploy.sh --services` lists. The set comes from the dependency graph that
`scripts/diff-build` already computes, not from changed paths, so a shared
library edit is labeled with every service that links it. The labels are
re-synced on each push; ones for services a PR no longer touches are removed,
and labels outside the `service:` prefix are never touched. A change that
forces a full build (a `.bazelrc` or shared-macro edit) has no trustworthy
impact set, so it gets no service labels and loses none: an unknown blast
radius is not reported as an empty one.
