# NCM

Experimental next-concept training and inference in Rust, using the same
Candle, HuggingFace tokenizers, safetensors, Serde, and Clap ecosystem as this
repository's microgpt. Built and tested with Bazel.

This implements the two-stage idea described in
[Next-Concept Modeling](https://arxiv.org/abs/2610.14162) and the
[reference repository](https://github.com/HazyResearch/ncm). It is a compact
independent variant, **not a reproduction of their benchmarks or a loader for
their PyTorch checkpoints**. There is no demonstrated Java/backend programming
competence yet. Tiny smoke runs verify the pipeline, not useful code generation.

## Quick start

Run from the repository root. The supplied Java, Go and TypeScript examples are
only enough for a smoke test. Use separate output paths for every experiment.

```bash
bazel test //domains/ai/libs/ncm/... //domains/ai/apps/ncm_cli/...
bazel build //domains/ai/apps/ncm_cli:ncm

bazel-bin/domains/ai/apps/ncm_cli/ncm train-tokenizer \
  --input domains/ai/apps/ncm_cli/testdata/tiny.jsonl \
  --config domains/ai/apps/ncm_cli/testdata/tiny-config.json \
  --output /tmp/ncm-vq --steps 200

bazel-bin/domains/ai/apps/ncm_cli/ncm train-generator \
  --input domains/ai/apps/ncm_cli/testdata/tiny.jsonl \
  --model /tmp/ncm-vq --output /tmp/ncm-model --steps 200

bazel-bin/domains/ai/apps/ncm_cli/ncm generate \
  --model /tmp/ncm-model --language java --prompt 'Define an empty class.'

bazel-bin/domains/ai/apps/ncm_cli/ncm evaluate \
  --model /tmp/ncm-model --input /path/to/held-out.jsonl
```

CPU is the default. macOS Bazel targets enable Metal; select `--device metal`.
Training uses F32. CUDA feature hooks exist in the Rust manifests, but a CUDA
Bazel toolchain/target configuration is not provided or validated. Do not assume
consumer-GPU training times or the paper's speedups apply to this implementation.

## Data and languages

JSONL records contain exactly `language`, `prompt`, and `completion`:

```json
{"language":"java","prompt":"Implement an idempotent payment handler using the supplied interface...","completion":"...complete source or an agent action..."}
```

Language is a conditioning string, not a hardcoded list. Train separate model
bundles or mix records for Java, Go, Rust, Python, TypeScript, SQL, etc. Supply
interfaces, framework versions and task constraints in prompts. Correct code,
tests and realistic task/answer pairs matter more than simply collecting files.

```bash
bazel-bin/domains/ai/apps/ncm_cli/ncm prepare \
  --root /path/to/repos --language java --extensions java \
  --output /tmp/java-files.jsonl
bazel-bin/domains/ai/apps/ncm_cli/ncm config > /tmp/ncm-config.json
```

`prepare` keeps whole files, whitespace and comments; deduplicates exact content;
skips symlinks, hidden directories, common build/vendor directories and files
larger than `--max-bytes` (default 65536). It emits a generic source-generation
prompt. Curate instruction examples separately; this command does not infer a
file's purpose. Review corpus licensing and split **by repository before
preparation**, keeping validation/test repositories separate from training.

Byte-level BPE has a complete byte alphabet for unseen identifiers and Unicode.
Only training data trains BPE. Two reserved IDs are PAD=0 and EOS=1. Literal
`<pad>`/`<eos>` strings are reserved and should not appear in code examples.
Overlong prompts/completions are rejected with their token count, never silently
truncated. Split examples at meaningful boundaries or increase model limits.
The dataset and encoded examples are currently loaded in memory.

## Architecture and training

1. BPE converts text to token IDs. A bidirectional transformer encodes fixed-size
   completion sequences. At each scale, mean pooling compresses the remaining
   latent residual; nearest-neighbor vector quantization selects codes from a
   shared trainable codebook. Nearest-neighbor expansion adds each scale's
   contribution. A bidirectional decoder predicts text tokens in parallel.
   Training combines reconstruction cross-entropy (excluding PAD), codebook MSE
   and commitment MSE, using a straight-through estimator.
2. A separate transformer receives prompt embeddings and the cumulative
   reconstruction from **coarser** levels. Each pass predicts all codes at one
   scale in parallel. Teacher-forced target codes update the reconstruction only
   **after** computing that level's loss. The tokenizer's tensors are detached
   and excluded from the generator optimizer. Loss weights positions equally.
3. Inference replaces teacher-forced codes with greedy predictions, makes one
   transformer evaluation per configured level, then decodes text once. Output
   ends at EOS or PAD. Prompt and output limits are distinct.

Unlike the reference's packed masked training, this implementation evaluates
levels separately with bidirectional attention inside each pass. This prevents
future-target leakage by construction, but is less efficient. It uses mean
pooling, nearest-neighbor expansion, a shared codebook and learned positions;
there is no claim that learned codes correspond to syntax trees or semantic
outlines. Full-sequence generation does not guarantee compilable code.

Defaults are intentionally small: width 64, 2 layers, 64 codes, length 128,
scales 1/2/4/8/16/32/64/128. Every scale must divide output length; width must
be divisible by heads. `config` prints editable JSON. Begin with short examples,
inspect reconstruction and code usage, then increase capacity. Attention costs
are quadratic in sequence length. Current optimization is AdamW with fixed LR,
one example per update, deterministic corpus order, and no mixed precision,
minibatching, distributed training or optimizer-resume support. Initial weights
are seeded on the host; accelerator numerical behavior can differ.

`evaluate` reports reconstruction loss (including VQ penalties), teacher-forced
code loss, non-PAD reconstruction and generation token accuracy, generation
exact match, and unique code counts per scale. Low code usage can indicate
collapse. Low reconstruction accuracy is a reason to improve stage one before
training stage two. These metrics are not code correctness: separately compile
and run held-out task tests in an appropriate isolated environment.

## Model bundles

Each output directory contains `config.json`, `tokenizer.json`,
`vq.safetensors`, `generator.safetensors`, and a versioned `manifest.json`
recording stage, step count, LR, seed and example count. Files are staged together
before publication; existing output directories are refused. Failed publication
may leave a `.partial-*` directory for inspection. Inference rejects bundles
that have only completed tokenizer training.

`train-generator --model existing-generator --output new-generator` fine-tunes
with a fresh optimizer and frozen tokenizer; this is **not exact resume**.
Tokenizer changes require a fresh generator. There are no periodic checkpoints;
use shorter generator training invocations to save intermediate bundles.

## Pluggable agent harness

```bash
bazel-bin/domains/ai/apps/ncm_cli/ncm agent \
  --model /path/to/action-trained-model --language java \
  --workspace /path/to/project --task 'Read src/Handler.java and suggest a fix.' \
  --max-steps 8
```

`agent::Backend` supplies model completions; `agent::Tool` supplies tools.
`Harness::register` attaches tool implementations. The CLI uses `LocalBackend`
and a bounded `read_file` tool. An adapter for another model runtime or pi can
implement `Backend` without changing the loop; no pi integration is bundled.
The harness exchanges JSON transcripts and accepts one action per response:

```json
{"type":"tool","name":"read_file","arguments":{"path":"src/Handler.java"}}
{"type":"final","content":"Suggested implementation..."}
```

To train agent behavior, put the serialized transcript (system, user, assistant,
and tool events, in the same format emitted by the harness) in `prompt`, and the
next action JSON in `completion`. Include multi-step tool observations and final
answers. Use prompt/output limits large enough for the protocol and source.
Ordinary source-file training does not teach this protocol. Invalid actions,
unknown tools, oversized context and exhausted step budgets fail explicitly.

The default tool cannot write files or execute commands. Canonical path checks
reject traversal and symlink escapes; this is not an OS sandbox against another
process concurrently mutating the workspace. Add edit/build tools through the
interface with appropriate isolation and authorization for your application.

## Verification

Bazel tests cover loss reduction in both training stages, exact tokenizer weight
preservation during generator training, safetensors reload equivalence, the
JSONL/BPE pipeline, Unicode round trips, invalid dimensions and input lengths,
stage gating, overwrite refusal, tool dispatch and file containment. A CLI smoke
test runs training, evaluation, generation and corpus preparation.
