# Card Games - C++ Implementation

This module provides a set of classes and utilities for building card games.

## Related Projects

- [Rust Implementation](../../rust/cards) (WIP)
- [Go Implementation](../../go/cards) (WIP)

## Components

- Top level module contains core components for building card games.
- [golf](./golf) is a mostly complete implementation of the game Golf.

## Building

This project uses Bazel for building:

```bash
bazel build //cpp/cards:...
```

## Testing

```bash
bazel test //cpp/cards:...
```

## IDE Support

For optimal development experience, use CLion with the Bazel plugin or VSCode with the compile commands extractor as described in the main [README](../../README.md).
