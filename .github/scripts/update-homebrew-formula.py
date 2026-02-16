#!/usr/bin/env python3
"""Update a Homebrew formula with new version, URLs, and SHA256 hashes.

Constructs full GitHub release URLs from the tool name and version,
then replaces the url + sha256 blocks for both macOS and Linux targets.
"""

import re
import sys


def update_formula(formula_path, tool_name, version, mac_sha, linux_sha):
    with open(formula_path) as f:
        content = f.read()
    original = content

    base = f"https://github.com/muchq/MoonBase/releases/download/{tool_name}-v{version}"
    mac_url = f"{base}/{tool_name}-{version}-aarch64-apple-darwin.tar.gz"
    linux_url = f"{base}/{tool_name}-{version}-x86_64-unknown-linux-gnu.tar.gz"

    # Update macOS url and sha256
    content = re.sub(
        r'url "[^"]*aarch64-apple-darwin[^"]*"\n(\s+)sha256 "[^"]*"',
        f'url "{mac_url}"\n\\1sha256 "{mac_sha}"',
        content,
    )

    # Update Linux url and sha256
    content = re.sub(
        r'url "[^"]*x86_64-unknown-linux-gnu[^"]*"\n(\s+)sha256 "[^"]*"',
        f'url "{linux_url}"\n\\1sha256 "{linux_sha}"',
        content,
    )

    if content == original:
        print(
            "Error: No changes made to formula. Regex patterns may not match.",
            file=sys.stderr,
        )
        sys.exit(1)

    with open(formula_path, "w") as f:
        f.write(content)


if __name__ == "__main__":
    if len(sys.argv) != 6:
        print(
            f"Usage: {sys.argv[0]} <formula_path> <tool_name> <version> <mac_sha> <linux_sha>",
            file=sys.stderr,
        )
        sys.exit(1)
    update_formula(*sys.argv[1:])
