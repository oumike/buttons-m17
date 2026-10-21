#!/bin/bash
# Host unit tests: everything hardware-free (config, backup, web guard; later
# m17/ protocol code). No device needed.
#
# Usage: scripts/test.sh [extra pio test args, e.g. -v]

set -euo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")/.."
exec pio test -e native "$@"
