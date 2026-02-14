# Wordchains

A solver for the [Doublets](https://en.wikipedia.org/wiki/Word_ladder) word puzzle invented by Lewis Carroll. Given two words, it finds sequences of words where each step changes exactly one letter.

```
cold → cord → corm → worm → warm
```

## Usage

### Quick search

```sh
wordchains cold warm
# or
wordchains search cold warm
```

Finds the shortest path between two words and prints it.

### Interactive REPL

```sh
wordchains repl [--dictionary-path <PATH>]
```

Starts an interactive session. If no dictionary path is given, the default system dictionary (`/usr/share/dict/words`) or the embedded graph is used.

REPL commands:

| Command | Description |
|---|---|
| `shortest <start> <end>` | Find one shortest path between two words |
| `all-shortest <start> <end>` | Find all shortest paths (same length) |
| `all-paths <start> <end>` | Find all paths (depth-limited to 50 words) |
| `read-dict <path>` | Load a dictionary file and rebuild the graph |
| `read-graph <path>` | Load a pre-built graph from a JSON file |
| `exit` / `quit` | Exit the REPL |

### Generate a graph

```sh
wordchains generate-graph --dictionary-path <PATH> [--output-path <PATH>]
```

Builds the word graph from a dictionary file and writes it as JSON. If `--output-path` is omitted, the graph is printed to stdout. Pre-built graphs can be loaded in the REPL with `read-graph`.

## Building

With Cargo:

```sh
cargo build --release -p wordchains-bin
```

With Bazel (embeds the dictionary graph into the binary):

```sh
bazel build //domains/games/apps/wordchains:wordchains
```

## How it works

Words are loaded from a dictionary and organized into a graph where edges connect words that differ by exactly one letter. Path-finding uses BFS for shortest paths and DFS with backtracking for exhaustive search. The graph is cached on disk using a SHA-256 digest of the word list to avoid rebuilding on repeated runs.
