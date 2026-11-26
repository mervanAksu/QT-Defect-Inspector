#!/usr/bin/env bash
# Ubuntu 24.04 (yerel ya da WSL2) için geliştirme ortamı kurulumu.
# Kullanım:  bash scripts/setup_ubuntu.sh
set -euo pipefail

sudo apt update
sudo apt install -y \
    build-essential gdb cmake ninja-build git pkg-config \
    qt6-base-dev qt6-base-dev-tools libgl1-mesa-dev \
    libopencv-dev \
    libgtest-dev \
    valgrind clang-format

echo
echo "=== Kurulu sürümler ==="
g++ --version | head -n1
cmake --version | head -n1
echo "Ninja  $(ninja --version)"
echo "Qt     $(qtpaths6 --qt-version 2>/dev/null || echo 'bulunamadi')"
echo "OpenCV $(pkg-config --modversion opencv4 2>/dev/null || echo 'bulunamadi')"
if [ -d /usr/lib/x86_64-linux-gnu/cmake/GTest ] || [ -d /usr/lib/cmake/GTest ]; then
    echo "GTest  CMake paketi mevcut"
else
    echo "GTest  CMake paketi BULUNAMADI"
fi
