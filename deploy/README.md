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
not be read — the PR gets `service:unknown` beside whatever service labels it
has and loses none, so an unknown blast radius is never mistaken for an empty
one; the next run that does know the answer retires the marker. A base commit
whose image publish has not pushed yet is waited for a few minutes; past that,
the graph's answer stands for it.

## Deploy history by service

`deploy.sh --list --service one_d4` lists the last commits that changed the
`one_d4` image, so a targeted deploy or rollback can be aimed without reading
every subject. It reads the published images themselves: a commit changed the
service when its image's content differs from the next older commit's, which
is ground truth rather than intent, and works on commits published long before
anything labeled them. The `<- deployed` marker sits on the change the running
image came from. A commit with no published image is skipped over and the row
says the change may sit in the gap. The scan covers the last 100 commits by
default and says so when it runs out before an answer; `DEPLOY_LIST_SCAN`
widens it.
