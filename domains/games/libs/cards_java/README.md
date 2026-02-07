# Cards - Java Implementation

This directory contains WIP Java implementations of some card games. It provides core card game mechanics and utilities that can be used to build various games.

## Related Projects

- [Rust Implementation](../../../../rust/cards)
- [C++ Implementation](../../../../cpp/cards)

## Building

This project uses Bazel for building:

```bash
bazel build //domains/games/libs/cards_java:...
```

## Testing

```bash
bazel test //domains/games/libs/cards_java:...
```

## TODO:
- [ ] Canasta rules implementation
- [ ] Canasta WS layer
