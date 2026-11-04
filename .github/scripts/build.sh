#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
set -euo pipefail

if [[ $(id -u) == 0 ]]; then
    echo 'Build checks must run as an unprivileged user.' >&2
    exit 1
fi

mkdir -p build-ci/artifacts
pacman -Q > build-ci/artifacts/packages.txt
clang-format --version > build-ci/artifacts/formatters.txt
/usr/lib/qt6/bin/qtpaths --qt-version >> build-ci/artifacts/formatters.txt
if ! clang-format --version | grep -Eq 'version 23\.'; then
    echo 'The formatting baseline requires clang-format 23.' >&2
    exit 1
fi
if [[ $(/usr/lib/qt6/bin/qtpaths --qt-version) != 6.11.* ]]; then
    echo 'The formatting baseline requires Qt tools 6.11.' >&2
    exit 1
fi

python3 tools/check.py > build-ci/artifacts/source-check.log 2>&1
cmake -S . -B build-ci -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo \
    -DBUILD_TESTING=ON -DBUILD_KWIN_EFFECT=ON > build-ci/artifacts/configure.log 2>&1
cmake --build build-ci --parallel 2 > build-ci/artifacts/build.log 2>&1
ctest --test-dir build-ci --output-on-failure \
    --output-junit artifacts/tests.xml > build-ci/artifacts/tests.log 2>&1
