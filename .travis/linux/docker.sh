#!/bin/bash -ex

apt-get update
apt-get install --no-install-recommends -y build-essential git libqt5opengl5-dev libsdl2-dev libssl-dev python qtbase5-dev qtwebengine5-dev wget cmake ninja-build ccache

cd /yuzu

mkdir build && cd build
cmake .. -DYUZU_USE_BUNDLED_UNICORN=ON -DYUZU_USE_QT_WEB_ENGINE=ON -DCMAKE_BUILD_TYPE=Release -DCMAKE_C_COMPILER=/usr/lib/ccache/gcc -DCMAKE_CXX_COMPILER=/usr/lib/ccache/g++ -DYUZU_ENABLE_COMPATIBILITY_REPORTING=${ENABLE_COMPATIBILITY_REPORTING:-"OFF"} -DENABLE_COMPATIBILITY_LIST_DOWNLOAD=ON -DUSE_DISCORD_PRESENCE=ON -G Ninja
ninja

ccache -s

ctest -VV -C Release
