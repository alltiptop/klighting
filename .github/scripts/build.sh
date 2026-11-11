#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
set -euo pipefail

if [[ $(id -u) == 0 ]]; then
    echo 'Build checks must run as an unprivileged user.' >&2
    exit 1
fi

mkdir -p build-ci/artifacts
pacman -Q > build-ci/artifacts/packages.txt
cp /opt/klighting-ci/package-sizes.txt build-ci/artifacts/package-sizes.txt
ccache --zero-stats
trap 'ccache --show-stats > build-ci/artifacts/ccache.txt' EXIT
clang_version=$(clang-format --version)
qt_version=$(/usr/lib/qt6/bin/qtpaths --qt-version)
printf '%s\n' "$clang_version" "Qt tools $qt_version" | tee build-ci/artifacts/formatters.txt
if [[ ! $clang_version =~ version\ 23\. ]]; then
    echo "The formatting baseline requires clang-format 23; found: $clang_version." >&2
    exit 1
fi
if [[ $qt_version != 6.11.* ]]; then
    echo "The formatting baseline requires Qt tools 6.11; found: $qt_version." >&2
    exit 1
fi

python3 tools/check.py > build-ci/artifacts/source-check.log 2>&1
cmake -S . -B build-ci -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo \
    -DBUILD_TESTING=ON -DBUILD_KWIN_EFFECT=ON \
    -DKLIGHTING_PULSEAUDIO=ON -DKLIGHTING_REQUIRE_AUDIO_TESTS=ON \
    -DCMAKE_CXX_COMPILER_LAUNCHER=ccache > build-ci/artifacts/configure.log 2>&1
cmake --build build-ci --parallel 2 > build-ci/artifacts/build.log 2>&1
ctest --test-dir build-ci --output-on-failure \
    --output-junit artifacts/tests.xml > build-ci/artifacts/tests.log 2>&1
cmake --build build-ci --target local_bundle > build-ci/artifacts/bundle.log 2>&1
tools/install-local.sh --check build-ci/local-bundle > build-ci/artifacts/bundle-check.log 2>&1
