#!/bin/bash

set -e

echo "===================================="
echo "  Terminal Emulator Installer"
echo "===================================="
echo ""

# Detect OS
if [[ "$OSTYPE" == "linux-gnu"* ]]; then
    echo "Detected Linux system"

    # Detect Linux distribution
    if [ -f /etc/arch-release ]; then
        echo "Distribution: Arch Linux"
        echo ""
        echo "Installing dependencies with pacman..."

        sudo pacman -S --needed \
            base-devel \
            clang \
            glfw \
            freetype2 \
            glew \
            libx11 \
            pkg-config

        echo ""
        echo "✓ Dependencies installed successfully on Arch Linux"

    elif [ -f /etc/debian_version ]; then
        # Debian or Ubuntu
        if grep -qi ubuntu /etc/os-release; then
            echo "Distribution: Ubuntu"
        else
            echo "Distribution: Debian"
        fi

        echo ""
        echo "Installing dependencies with apt..."

        sudo apt update
        sudo apt install -y \
            build-essential \
            clang \
            libglfw3-dev \
            libfreetype6-dev \
            libglew-dev \
            libx11-dev \
            pkg-config

        echo ""
        echo "✓ Dependencies installed successfully on Debian/Ubuntu"

    else
        echo "Error: Unsupported Linux distribution"
        echo "This script supports Arch Linux, Debian, and Ubuntu"
        exit 1
    fi

elif [[ "$OSTYPE" == "darwin"* ]]; then
    echo "Detected macOS"
    echo ""

    # Check if Homebrew is installed
    if ! command -v brew &> /dev/null; then
        echo "Error: Homebrew is not installed"
        echo "Please install Homebrew first: https://brew.sh"
        exit 1
    fi

    echo "Installing dependencies with Homebrew..."

    brew install \
        glfw \
        freetype \
        pkg-config

    # Check if Xcode Command Line Tools are installed
    if ! xcode-select -p &> /dev/null; then
        echo ""
        echo "Installing Xcode Command Line Tools..."
        xcode-select --install
        echo "Please complete the Xcode Command Line Tools installation and run this script again"
        exit 0
    fi

    echo ""
    echo "✓ Dependencies installed successfully on macOS"

else
    echo "Error: Unsupported operating system: $OSTYPE"
    exit 1
fi

echo ""
echo "===================================="
echo "  Installation Complete!"
echo "===================================="
echo ""
echo "You can now build the project with:"
echo "  make"
echo ""
echo "And run it with:"
echo "  ./term"
echo ""
