# LeetCode Solutions - C++ Implementation

This directory contains C++ implementations of LeetCode problems. It serves as a collection of solutions and reference implementations for various algorithmic challenges.

## Features

- Algorithm implementations
- Data structure solutions
- Performance optimizations
- Test cases
- Documentation

## Building

This project uses Bazel for building:

```bash
bazel build //domains/leet/libs/leet_cpp:...
```

## Testing

```bash
bazel test //domains/leet/libs/leet_cpp:...
```

## Example Usage

```cpp
// Example of using a solution
#include "so_leet/solution.h"

int main() {
    Solution solution;
    std::vector<int> input = {1, 2, 3, 4, 5};
    auto result = solution.solve(input);
    return 0;
}
```

## IDE Support

For optimal development experience, use CLion with the Bazel plugin or VSCode with the compile commands extractor as described in the main [README](../../README.md).
