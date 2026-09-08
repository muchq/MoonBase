# Deploy

Deploy metadata for MoonBase services

## Service labels on PRs

CI labels every PR with `service:<name>` for each image the change reaches —
`service:mithril`, `service:stats`, and so on — using the same names
`deploy.sh --services` lists. Candidates come from the dependency graph that
`scripts/diff-build` already computes, not from changed paths, so a shared
library edit reaches every service that links it. Each candidate is then
confirmed by building its image and comparing the content digests with the
image published for the PR's merge base: the label means the image changed,
not that the graph said it might. A reworded comment, a dependency nothing
links, or a change that forces a full build (a `.bazelrc` or shared-macro
edit) and rebuilds every image identically all reach an image without
changing it, and get no label for it.

The labels are re-synced on each push; ones for services a PR no longer
touches are removed, and labels outside the `service:` prefix are never
touched. When the answer is unknown — a build failed, or the registry could
not be read — the PR gets no service labels and loses none: an unknown blast
radius is not reported as an empty one. A base commit whose image publish
has not pushed yet is waited for a few minutes; past that, the graph's answer
stands for it.
