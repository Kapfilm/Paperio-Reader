#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")"
export CROSSPOINT_RELEASE_VERSION=2.2.6-rc.3
python3 scripts/gen_i18n.py --strip-unused
pio run -e gh_release "$@"
