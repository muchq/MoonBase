# ncm CLI

Bazel-built Rust CLI for experimental two-stage next-concept training,
evaluation, generation, and a pluggable agent loop.

See the [NCM guide](../../libs/ncm/README.md) for commands, data format,
architecture, model bundles, agent protocol, and limitations.

```bash
bazel build //domains/ai/apps/ncm_cli:ncm
bazel-bin/domains/ai/apps/ncm_cli/ncm --help
bazel test //domains/ai/libs/ncm/... //domains/ai/apps/ncm_cli/...
```
