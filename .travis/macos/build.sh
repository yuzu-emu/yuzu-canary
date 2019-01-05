#!/bin/bash -ex

set -o pipefail

export MACOSX_DEPLOYMENT_TARGET=10.13
export Qt5_DIR=$(brew --prefix)/opt/qt5
export UNICORNDIR=$(pwd)/externals/unicorn
export PATH="/usr/local/opt/ccache/libexec:$PATH"

mkdir build && cd build
cmake --version
cmake .. -DYUZU_USE_BUNDLED_UNICORN=ON -DYUZU_USE_QT_WEB_ENGINE=ON -DCMAKE_BUILD_TYPE=Release -DENABLE_COMPATIBILITY_LIST_DOWNLOAD=ON -DYUZU_ENABLE_COMPATIBILITY_REPORTING=${ENABLE_COMPATIBILITY_REPORTING:-"OFF"} -DUSE_DISCORD_PRESENCE=ON
make -j4

ccache -s

ctest -VV -C Release
