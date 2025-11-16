#!/bin/bash
set -e

echo "=== Current directory: $(pwd) ==="
echo "=== Listing src directory: ==="
ls -la src/ 2>/dev/null || echo "src/ not found"
echo "=== Finding test files: ==="
find . -name "*.test.ts*" 2>/dev/null | head -20 || echo "No test files found"
echo "=== Running vitest ==="
exec "$@"
