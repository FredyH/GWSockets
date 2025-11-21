#!/bin/bash
if [ ! -d "$HOME/vcpkg" ]; then
    echo "vcpkg not found. Installing..."
    git clone https://github.com/Microsoft/vcpkg.git ~/vcpkg
    ~/vcpkg/bootstrap-vcpkg.sh
fi

mkdir -p build && cd build
cmake .. -DCMAKE_TOOLCHAIN_FILE=~/vcpkg/scripts/buildsystems/vcpkg.cmake
cmake --build .