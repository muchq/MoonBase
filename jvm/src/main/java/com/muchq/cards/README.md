# Cards - Java Implementation

This directory contains WIP Java implementations of some card games. It provides core card game mechanics and utilities that can be used to build various games.

## Related Projects

- [Rust Implementation](../../../../rust/cards)
- [C++ Implementation](../../../../cpp/cards)
- [Go Implementation](../../../../go/cards)

## Building

This project uses Bazel for building:

```bash
bazel build //jvm/src/main/java/com/muchq/cards:...
```

## Testing

```bash
bazel test //jvm/src/main/java/com/muchq/cards:...
```

## TODO:
- [ ] Canasta rules implementation
- [ ] Canasta WS layer
