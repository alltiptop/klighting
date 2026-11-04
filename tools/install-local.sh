#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
set -euo pipefail
exec python3 "$(dirname -- "$(realpath -- "$0")")/install_local.py" "$@"
