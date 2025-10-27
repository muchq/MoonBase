#!/bin/bash

# Build Pal - Deployment Test Script
# Tests the build artifacts and installation process

set -e

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

DIST_DIR="dist"
TEST_DIR="test_install"

echo -e "${BLUE}Testing Build Pal Deployment${NC}"
echo "============================="

# Check if distribution exists
if [ ! -d "$DIST_DIR" ]; then
    echo -e "${RED}Error: Distribution directory not found. Run build.sh first.${NC}"
    exit 1
fi

# Clean previous test
echo -e "${YELLOW}Cleaning previous test installation...${NC}"
rm -rf "$TEST_DIR"
mkdir -p "$TEST_DIR"

# Test 1: Verify binaries exist
echo -e "${YELLOW}Test 1: Verifying binaries exist...${NC}"
if [ ! -f "$DIST_DIR/build_pal" ]; then
    echo -e "${RED}❌ CLI binary not found${NC}"
    exit 1
fi

if [ ! -f "$DIST_DIR/build_pal_server" ]; then
    echo -e "${RED}❌ Server binary not found${NC}"
    exit 1
fi

echo -e "${GREEN}✅ Binaries found${NC}"

# Test 2: Verify binaries are executable
echo -e "${YELLOW}Test 2: Verifying binaries are executable...${NC}"
if [ ! -x "$DIST_DIR/build_pal" ]; then
    echo -e "${RED}❌ CLI binary is not executable${NC}"
    exit 1
fi

if [ ! -x "$DIST_DIR/build_pal_server" ]; then
    echo -e "${RED}❌ Server binary is not executable${NC}"
    exit 1
fi

echo -e "${GREEN}✅ Binaries are executable${NC}"

# Test 3: Test CLI binary basic functionality
echo -e "${YELLOW}Test 3: Testing CLI binary basic functionality...${NC}"
if ! "$DIST_DIR/build_pal" --help > /dev/null 2>&1; then
    echo -e "${RED}❌ CLI binary --help failed${NC}"
    exit 1
fi

echo -e "${GREEN}✅ CLI binary responds to --help${NC}"

# Test 4: Test server binary basic functionality
echo -e "${YELLOW}Test 4: Testing server binary basic functionality...${NC}"
# Start server in background and test if it starts
timeout 5s "$DIST_DIR/build_pal_server" > /dev/null 2>&1 &
SERVER_PID=$!
sleep 2

# Check if server is still running (it should bind to port and run)
if ! kill -0 $SERVER_PID 2>/dev/null; then
    echo -e "${RED}❌ Server binary failed to start${NC}"
    exit 1
fi

# Clean up server
kill $SERVER_PID 2>/dev/null || true
wait $SERVER_PID 2>/dev/null || true

echo -e "${GREEN}✅ Server binary starts successfully${NC}"

# Test 5: Test installation script
echo -e "${YELLOW}Test 5: Testing installation script...${NC}"
if [ ! -f "$DIST_DIR/install.sh" ]; then
    echo -e "${RED}❌ Installation script not found${NC}"
    exit 1
fi

if [ ! -x "$DIST_DIR/install.sh" ]; then
    echo -e "${RED}❌ Installation script is not executable${NC}"
    exit 1
fi

# Test installation to test directory
cd "$DIST_DIR"
./install.sh --install-dir "../$TEST_DIR" > /dev/null 2>&1
cd ..

# Verify installation
if [ ! -f "$TEST_DIR/build_pal" ] || [ ! -f "$TEST_DIR/build_pal_server" ]; then
    echo -e "${RED}❌ Installation script failed to copy binaries${NC}"
    exit 1
fi

if [ ! -x "$TEST_DIR/build_pal" ] || [ ! -x "$TEST_DIR/build_pal_server" ]; then
    echo -e "${RED}❌ Installed binaries are not executable${NC}"
    exit 1
fi

echo -e "${GREEN}✅ Installation script works correctly${NC}"

# Test 6: Test installed binaries
echo -e "${YELLOW}Test 6: Testing installed binaries...${NC}"
if ! "$TEST_DIR/build_pal" --help > /dev/null 2>&1; then
    echo -e "${RED}❌ Installed CLI binary --help failed${NC}"
    exit 1
fi

echo -e "${GREEN}✅ Installed binaries work correctly${NC}"

# Test 7: Verify distribution files
echo -e "${YELLOW}Test 7: Verifying distribution files...${NC}"
REQUIRED_FILES=("build_pal" "build_pal_server" "install.sh" "README.md" "VERSION")

for file in "${REQUIRED_FILES[@]}"; do
    if [ ! -f "$DIST_DIR/$file" ]; then
        echo -e "${RED}❌ Required file missing: $file${NC}"
        exit 1
    fi
done

echo -e "${GREEN}✅ All required distribution files present${NC}"

# Test 8: Verify VERSION file content
echo -e "${YELLOW}Test 8: Verifying VERSION file content...${NC}"
if ! grep -q "Build Pal v" "$DIST_DIR/VERSION"; then
    echo -e "${RED}❌ VERSION file missing version information${NC}"
    exit 1
fi

if ! grep -q "Target:" "$DIST_DIR/VERSION"; then
    echo -e "${RED}❌ VERSION file missing target information${NC}"
    exit 1
fi

if ! grep -q "Built:" "$DIST_DIR/VERSION"; then
    echo -e "${RED}❌ VERSION file missing build timestamp${NC}"
    exit 1
fi

echo -e "${GREEN}✅ VERSION file contains required information${NC}"

# Test 9: Test binary sizes (should be reasonable)
echo -e "${YELLOW}Test 9: Checking binary sizes...${NC}"
CLI_SIZE=$(stat -f%z "$DIST_DIR/build_pal" 2>/dev/null || stat -c%s "$DIST_DIR/build_pal" 2>/dev/null)
SERVER_SIZE=$(stat -f%z "$DIST_DIR/build_pal_server" 2>/dev/null || stat -c%s "$DIST_DIR/build_pal_server" 2>/dev/null)

# Binaries should be at least 1MB (reasonable for Rust binaries)
if [ "$CLI_SIZE" -lt 1048576 ]; then
    echo -e "${RED}❌ CLI binary seems too small ($CLI_SIZE bytes)${NC}"
    exit 1
fi

if [ "$SERVER_SIZE" -lt 1048576 ]; then
    echo -e "${RED}❌ Server binary seems too small ($SERVER_SIZE bytes)${NC}"
    exit 1
fi

# Binaries shouldn't be excessively large (>100MB is suspicious)
if [ "$CLI_SIZE" -gt 104857600 ]; then
    echo -e "${YELLOW}⚠️  CLI binary is quite large ($CLI_SIZE bytes)${NC}"
fi

if [ "$SERVER_SIZE" -gt 104857600 ]; then
    echo -e "${YELLOW}⚠️  Server binary is quite large ($SERVER_SIZE bytes)${NC}"
fi

echo -e "${GREEN}✅ Binary sizes are reasonable${NC}"

# Test 10: Test README content
echo -e "${YELLOW}Test 10: Verifying README content...${NC}"
if ! grep -q "Build Pal v" "$DIST_DIR/README.md"; then
    echo -e "${RED}❌ README missing version information${NC}"
    exit 1
fi

if ! grep -q "Installation" "$DIST_DIR/README.md"; then
    echo -e "${RED}❌ README missing installation instructions${NC}"
    exit 1
fi

if ! grep -q "Quick Start" "$DIST_DIR/README.md"; then
    echo -e "${RED}❌ README missing quick start guide${NC}"
    exit 1
fi

echo -e "${GREEN}✅ README contains required sections${NC}"

# Cleanup
echo -e "${YELLOW}Cleaning up test files...${NC}"
rm -rf "$TEST_DIR"

# Summary
echo ""
echo -e "${GREEN}🎉 All deployment tests passed!${NC}"
echo "=================================="
echo "Distribution is ready for deployment."
echo ""
echo "Distribution contents:"
ls -la "$DIST_DIR/"
echo ""
echo "Binary sizes:"
ls -lh "$DIST_DIR/build_pal" "$DIST_DIR/build_pal_server" | awk '{print "  " $9 ": " $5}'