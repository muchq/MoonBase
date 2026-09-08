# Deploy

Deploy metadata for MoonBase services

## Service labels on PRs

CI labels every PR with `service:<name>` for each image the change reaches —
`service:mithril`, `service:stats`, and so on — using the same names
`deploy.sh --services` lists. Candidates come from the dependency graph that
`scripts/diff-build` already computes, not from changed paths, so a shared
library edit reaches every service that links it. Each candidate is then
confirmed by building its image and comparing the content digests with the
image published for the PR's merge base, so a test-only edit under a
service's package is not labeled: the label means the image changed, not
that the graph said it might. A change that forces a full build (a
`.bazelrc` or shared-macro edit) takes every service as a candidate and
reports the ones whose image actually changed.

The labels are re-synced on each push; ones for services a PR no longer
touches are removed, and labels outside the `service:` prefix are never
touched. When the answer is unknown — the base commit's image is not
published yet, or a build failed — the PR gets no service labels and loses
none: an unknown blast radius is not reported as an empty one.
