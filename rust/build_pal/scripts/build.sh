#!/bin/bash

# Build Pal - Build Script
# Generates distributable binaries for CLI and server components

set -e

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# Configuration
BUILD_DIR="target/release"
DIST_DIR="dist"
VERSION=${BUILD_PAL_VERSION:-"0.1.0"}
TARGET=${BUILD_TARGET:-""}

echo -e "${BLUE}Building Build Pal v${VERSION}${NC}"
echo "=================================="

# Clean previous builds
echo -e "${YELLOW}Cleaning previous builds...${NC}"
rm -rf "${DIST_DIR}"
mkdir -p "${DIST_DIR}"

# Determine target architecture
if [ -z "$TARGET" ]; then
    TARGET=$(rustc -vV | sed -n 's|host: ||p')
    echo -e "${BLUE}Auto-detected target: ${TARGET}${NC}"
else
    echo -e "${BLUE}Using specified target: ${TARGET}${NC}"
fi

# Build CLI binary
echo -e "${YELLOW}Building CLI binary...${NC}"
if [ -n "$TARGET" ] && [ "$TARGET" != "$(rustc -vV | sed -n 's|host: ||p')" ]; then
    # Cross-compilation
    cargo build --release --target "$TARGET" --bin build_pal_cli
    CLI_BINARY="target/${TARGET}/release/build_pal_cli"
else
    # Native compilation
    cargo build --release --bin build_pal_cli
    CLI_BINARY="target/release/build_pal_cli"
fi

# Build server binary
echo -e "${YELLOW}Building server binary...${NC}"
if [ -n "$TARGET" ] && [ "$TARGET" != "$(rustc -vV | sed -n 's|host: ||p')" ]; then
    # Cross-compilation
    cargo build --release --target "$TARGET" --bin build_pal_server
    SERVER_BINARY="target/${TARGET}/release/build_pal_server"
else
    # Native compilation
    cargo build --release --bin build_pal_server
    SERVER_BINARY="target/release/build_pal_server"
fi

# Verify binaries exist
if [ ! -f "$CLI_BINARY" ]; then
    echo -e "${RED}Error: CLI binary not found at $CLI_BINARY${NC}"
    exit 1
fi

if [ ! -f "$SERVER_BINARY" ]; then
    echo -e "${RED}Error: Server binary not found at $SERVER_BINARY${NC}"
    exit 1
fi

# Copy binaries to dist directory
echo -e "${YELLOW}Copying binaries to distribution directory...${NC}"
cp "$CLI_BINARY" "${DIST_DIR}/build_pal"
cp "$SERVER_BINARY" "${DIST_DIR}/build_pal_server"

# Make binaries executable
chmod +x "${DIST_DIR}/build_pal"
chmod +x "${DIST_DIR}/build_pal_server"

# Create version info
echo -e "${YELLOW}Creating version info...${NC}"
cat > "${DIST_DIR}/VERSION" << EOF
Build Pal v${VERSION}
Target: ${TARGET}
Built: $(date -u +"%Y-%m-%d %H:%M:%S UTC")
Git Commit: $(git rev-parse --short HEAD 2>/dev/null || echo "unknown")
EOF

# Create installation script
echo -e "${YELLOW}Creating installation script...${NC}"
cat > "${DIST_DIR}/install.sh" << 'EOF'
#!/bin/bash

# Build Pal Installation Script

set -e

# Colors
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
RED='\033[0;31m'
NC='\033[0m'

# Default installation directory
INSTALL_DIR="${HOME}/.local/bin"

# Parse command line arguments
while [[ $# -gt 0 ]]; do
    case $1 in
        --install-dir)
            INSTALL_DIR="$2"
            shift 2
            ;;
        --help)
            echo "Build Pal Installation Script"
            echo ""
            echo "Usage: $0 [OPTIONS]"
            echo ""
            echo "Options:"
            echo "  --install-dir DIR    Installation directory (default: ~/.local/bin)"
            echo "  --help              Show this help message"
            exit 0
            ;;
        *)
            echo -e "${RED}Unknown option: $1${NC}"
            exit 1
            ;;
    esac
done

echo -e "${GREEN}Installing Build Pal...${NC}"
echo "Installation directory: $INSTALL_DIR"

# Create installation directory if it doesn't exist
mkdir -p "$INSTALL_DIR"

# Copy binaries
echo -e "${YELLOW}Copying binaries...${NC}"
cp build_pal "$INSTALL_DIR/"
cp build_pal_server "$INSTALL_DIR/"

# Make sure they're executable
chmod +x "$INSTALL_DIR/build_pal"
chmod +x "$INSTALL_DIR/build_pal_server"

echo -e "${GREEN}Installation complete!${NC}"
echo ""
echo "Add $INSTALL_DIR to your PATH if it's not already there:"
echo "  export PATH=\"$INSTALL_DIR:\$PATH\""
echo ""
echo "You can now run:"
echo "  build_pal --help"
EOF

chmod +x "${DIST_DIR}/install.sh"

# Create README
echo -e "${YELLOW}Creating distribution README...${NC}"
cat > "${DIST_DIR}/README.md" << EOF
# Build Pal v${VERSION}

Build Pal is a unified command-line interface for triggering build and test actions across different project types.

## Installation

Run the installation script:

\`\`\`bash
./install.sh
\`\`\`

Or manually copy the binaries to a directory in your PATH:

\`\`\`bash
cp build_pal /usr/local/bin/
cp build_pal_server /usr/local/bin/
\`\`\`

## Quick Start

1. Create a \`.build_pal\` configuration file in your project root:

\`\`\`json
{
  "tool": "bazel",
  "name": "my-project",
  "mode": "async",
  "retention": "all",
  "environment": "native"
}
\`\`\`

2. Run a build command:

\`\`\`bash
build_pal build //...
\`\`\`

## Files

- \`build_pal\` - CLI client
- \`build_pal_server\` - Background server (auto-started by CLI)
- \`install.sh\` - Installation script
- \`VERSION\` - Version and build information

## Documentation

For complete documentation, visit: https://build-pal.dev

## Support

- GitHub: https://github.com/build-pal/build-pal
- Issues: https://github.com/build-pal/build-pal/issues
EOF

# Display build summary
echo ""
echo -e "${GREEN}Build completed successfully!${NC}"
echo "=================================="
echo "Distribution directory: ${DIST_DIR}/"
echo "CLI binary: ${DIST_DIR}/build_pal"
echo "Server binary: ${DIST_DIR}/build_pal_server"
echo "Installation script: ${DIST_DIR}/install.sh"
echo ""

# Display binary sizes
echo "Binary sizes:"
ls -lh "${DIST_DIR}/build_pal" "${DIST_DIR}/build_pal_server" | awk '{print "  " $9 ": " $5}'

echo ""
echo -e "${GREEN}Ready for distribution!${NC}"