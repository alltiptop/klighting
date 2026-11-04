#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
set -euo pipefail

# Run only while building the CI image, never on the developer's host.
printf '%s\n' 'Server = https://archive.archlinux.org/repos/2026/10/01/$repo/os/$arch' > /etc/pacman.d/mirrorlist
# Keep licenses, headers, QML plugins and translations used by the test suite.
sed -i '/^\[options\]/a NoExtract = usr/share/doc/* usr/share/man/* usr/share/info/* usr/share/cmake/Help/* usr/share/wallpapers/*' /etc/pacman.conf
mapfile -t packages < /ci/packages.arch
# Archive downloads can pause long enough to trip libcurl's low-speed timeout.
# The workflow timeout still bounds the complete image build.
pacman -Syyuu --noconfirm --needed --disable-download-timeout "${packages[@]}"
find /var/cache/pacman/pkg -mindepth 1 -delete
useradd --create-home --uid 1001 builder
mkdir -p /opt/klighting-ci
pacman -Q > /opt/klighting-ci/packages.txt
LC_ALL=C pacman -Qi | awk '/^Name / { name=$3 } /^Installed Size/ { print name, $4, $5 }' \
    > /opt/klighting-ci/package-sizes.txt
