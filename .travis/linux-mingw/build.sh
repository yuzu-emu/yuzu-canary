#!/bin/bash -ex
mkdir "$HOME/.ccache" || true
docker run --env-file .travis/common/travis-ci.env -v $(pwd):/yuzu -v "$HOME/.ccache":/root/.ccache yuzuemu/build-environments:linux-mingw /bin/bash -ex /yuzu/.travis/linux-mingw/docker.sh
