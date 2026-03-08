#!/bin/sh
# install.sh — download and install a herescript release binary.
#
# Usage:
#   curl -fsSL https://raw.githubusercontent.com/sfkleach/herescript/main/install.sh | sh
#
# By default the binary is installed to $HOME/.local/bin. Override with:
#   INSTALL_DIR=/usr/local/bin sh install.sh
#
# To install a specific version (including pre-releases), set VERSION:
#   VERSION=v0.1.0-rc1 sh install.sh

set -e

REPO="sfkleach/herescript"
INSTALL_DIR="${INSTALL_DIR:-$HOME/.local/bin}"

# Detect OS and pick the matching release asset.
OS="$(uname -s)"
case "$OS" in
    Linux)  ASSET="herescript-linux" ;;
    Darwin) ASSET="herescript-macos" ;;
    *)
        echo "Unsupported operating system: $OS" >&2
        echo "Please build from source: https://github.com/$REPO" >&2
        exit 1
        ;;
esac

# Build the download URL. When VERSION is set use the specific tag; otherwise
# use the /releases/latest/ redirect (stable releases only).
if [ -n "$VERSION" ]; then
    URL="https://github.com/$REPO/releases/download/$VERSION/$ASSET"
else
    URL="https://github.com/$REPO/releases/latest/download/$ASSET"
fi

DEST="$INSTALL_DIR/herescript"

# Create the install directory if it does not already exist.
mkdir -p "$INSTALL_DIR"

echo "Downloading $URL ..."
if command -v curl > /dev/null 2>&1; then
    curl -fsSL "$URL" -o "$DEST"
elif command -v wget > /dev/null 2>&1; then
    wget -qO "$DEST" "$URL"
else
    echo "Neither curl nor wget found. Please install one and retry." >&2
    exit 1
fi

chmod +x "$DEST"
echo "Installed herescript to $DEST"
