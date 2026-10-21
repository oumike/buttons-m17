#!/bin/bash
# Flash buttons-m17 to a connected board. Modeled on camillia-mt's flash.sh.
# Expects a merged factory image (bootloader + partitions + boot_app0 + app)
# from scripts/package.sh, written at 0x0. An app-only firmware.bin or
# -ota.bin is NOT compatible with this script.
#
# Usage: scripts/flash.sh [--erase|-E] <firmware.bin | env> [port]
#   env           e.g. wio-tracker-l2: flashes the newest
#                 dist/buttons-m17-<env>-vX.Y.Z.bin.
#   --erase, -E   Erase the whole chip first. Wipes settings (Wi-Fi, admin
#                 password, callsign) - save a backup from the web UI first.
# Default port: /dev/ttyUSB0 (Linux) - use /dev/cu.usbmodem* on macOS.

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$SCRIPT_DIR/.."
# shellcheck source=lib/pio_core.sh
source "$SCRIPT_DIR/lib/pio_core.sh"
CORE_DIR="$(pio_default_core_dir "$ROOT")"

ERASE=0
ARGS=()
for arg in "$@"; do
    case "$arg" in
        --erase|-E) ERASE=1 ;;
        *)          ARGS+=("$arg") ;;
    esac
done

TARGET=${ARGS[0]:-}
PORT=${ARGS[1]:-/dev/ttyUSB0}

usage() {
    echo "Usage: scripts/flash.sh [--erase|-E] <buttons-m17-<env>-vX.Y.Z.bin | env> [port]" >&2
}

if [[ -z "$TARGET" ]]; then
    usage
    exit 1
fi

if [[ -f "$TARGET" ]]; then
    FIRMWARE="$TARGET"
else
    ENV="$TARGET"
    FIRMWARE=""
    for f in "$ROOT"/dist/buttons-m17-"$ENV"-v*.bin; do
        [[ -f "$f" ]] || continue
        case "$f" in *-ota.bin) continue ;; esac
        FIRMWARE="$(printf '%s\n%s\n' "$FIRMWARE" "$f" | sed '/^$/d' | sort -V | tail -1)"
    done
    if [[ -z "$FIRMWARE" ]]; then
        echo "No factory image for '$TARGET' in dist/ (run scripts/package.sh $TARGET)." >&2
        usage
        exit 1
    fi
fi

# A factory image carries the partition table at 0x8000 (magic AA 50). An
# app-only image does not, and written at 0x0 it replaces the bootloader.
PT_MAGIC="$(od -An -tx1 -j 32768 -N 2 "$FIRMWARE" 2>/dev/null | tr -d ' \n')"
if [[ "$PT_MAGIC" != "aa50" ]]; then
    echo "$FIRMWARE is not a merged factory image (no partition table at 0x8000)." >&2
    exit 1
fi

if command -v esptool.py >/dev/null 2>&1; then
    ESPTOOL=(esptool.py)
else
    PY="$CORE_DIR/penv/bin/python"
    [[ -x "$PY" ]] || PY=python3
    ESPTOOL=("$PY" "$CORE_DIR/packages/tool-esptoolpy/esptool.py")
fi

if [[ "$ERASE" == "1" ]]; then
    echo "Erasing $PORT (settings will be lost)..."
    "${ESPTOOL[@]}" --chip esp32s3 --port "$PORT" --baud 921600 \
        --before default_reset --after no_reset erase_flash
fi

echo "Flashing $FIRMWARE to $PORT..."
"${ESPTOOL[@]}" --chip esp32s3 --port "$PORT" --baud 921600 \
    --before default_reset --after hard_reset \
    write_flash -z 0x0 "$FIRMWARE"
