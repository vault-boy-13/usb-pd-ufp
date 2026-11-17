#!/usr/bin/env bash
set -euo pipefail
cd -- "$(dirname -- "$0")"
export DEVKITPRO="${DEVKITPRO:-/opt/devkitpro}"
export DEVKITA64="${DEVKITA64:-$DEVKITPRO/devkitA64}"
export PATH="$DEVKITPRO/tools/bin:$DEVKITA64/bin:/usr/bin:$PATH"
make --no-print-directory "$@"
