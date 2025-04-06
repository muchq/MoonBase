# Utility Library - C++ Implementation

This directory contains a C++ utility library. It provides a collection of commonly used utilities and helper functions for C++ development.

## Features

- String manipulation
- File operations
- Memory management
- Thread utilities
- Logging
- Error handling
- Type conversions

## Building

This project uses Bazel for building:

```bash
bazel build //cpp/futility:...
```

## Testing

```bash
bazel test //cpp/futility:...
```

## Example Usage

```cpp
// Example of using utility functions
#include "futility/string_utils.h"
#include "futility/file_utils.h"

int main() {
    // String manipulation
    std::string input = "Hello, World!";
    auto trimmed = futility::Trim(input);

    // File operations
    auto content = futility::ReadFile("example.txt");
    return 0;
}
```

## IDE Support

For optimal development experience, use CLion with the Bazel plugin or VSCode with the compile commands extractor as described in the main [README](../../README.md).
