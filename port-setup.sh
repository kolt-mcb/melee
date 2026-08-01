#!/bin/bash
# ============================================================
# Melee PC Port — System Dependencies Installer
# ============================================================
# Installs all required packages for building the PC port.
# Run as: sudo ./port-setup.sh
# ============================================================

set -e

echo "=========================================="
echo " Melee PC Port — Dependency Installer"
echo "=========================================="
echo ""

# Detect package manager
if command -v apt-get &> /dev/null; then
    PKG_MGR="apt"
elif command -v dnf &> /dev/null; then
    PKG_MGR="dnf"
elif command -v yum &> /dev/null; then
    PKG_MGR="yum"
elif command -v pacman &> /dev/null; then
    PKG_MGR="pacman"
else
    echo "❌ Unsupported package manager"
    exit 1
fi

echo "Using package manager: $PKG_MGR"
echo ""

case "$PKG_MGR" in
    apt)
        echo "Installing dependencies via apt..."
        sudo apt-get update
        sudo apt-get install -y \
            build-essential \
            clang \
            libsdl2-dev \
            libgl1-mesa-dev \
            libglu1-mesa-dev \
            cmake \
            pkg-config \
            git \
            python3 \
            python3-pip
        ;;
    dnf)
        echo "Installing dependencies via dnf..."
        sudo dnf install -y \
            gcc-c++ \
            clang \
            SDL2-devel \
            mesa-libGL-devel \
            mesa-libGLU-devel \
            cmake \
            pkg-config \
            git \
            python3
        ;;
    pacman)
        echo "Installing dependencies via pacman..."
        sudo pacman -S --noconfirm \
            gcc \
            clang \
            sdl2 \
            mesa \
            cmake \
            pkgconf \
            git \
            python
        ;;
    yum)
        echo "Installing dependencies via yum..."
        sudo yum install -y \
            gcc-c++ \
            clang \
            SDL2-devel \
            mesa-libGL-devel \
            cmake \
            pkgconfig \
            git \
            python3
        ;;
esac

echo ""
echo "=========================================="
echo " Dependencies Installed Successfully!"
echo "=========================================="
echo ""
echo "Next steps:"
echo "  1. cd /path/to/melee"
echo "  2. python3 configure.py          # Build GCN version first"
echo "  3. ninja                         # Verify GCN build"
echo "  4. python3 configure_pc.py       # Generate PC port build"
echo "  5. ninja -f build.ninja.pc       # Build PC port"
echo ""
echo "Note: The GCN build must succeed before the PC port"
echo "      can link against the compiled decomp objects."
echo ""
