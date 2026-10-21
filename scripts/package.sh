#!/bin/bash
# Build merged factory images (and app-only OTA images) into dist/.
# Modeled on the merge step of camillia-mt's scripts/release.sh.
#
# Usage: scripts/package.sh [env ...]      (default: every device env)
#
# Output per env, with vX.Y.Z from the VERSION file:
#   dist/buttons-m17-<env>-vX.Y.Z.bin       factory image, flash at 0x0
#   dist/buttons-m17-<env>-vX.Y.Z-ota.bin   app only, for web OTA (milestone 7)
#   dist/buttons-m17-<env>-vX.Y.Z.elf       for decoding crash backtraces

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$SCRIPT_DIR/.."
cd "$ROOT"
# shellcheck source=lib/pio_core.sh
source "$SCRIPT_DIR/lib/pio_core.sh"

CORE_DIR="$(pio_default_core_dir "$(pwd)")"
VERSION="$(tr -d '[:space:]' < VERSION)"

ENVS=("$@")
if [ "${#ENVS[@]}" -eq 0 ]; then
	while IFS= read -r env; do
		[ -n "$env" ] && ENVS+=("$env")
	done < <(sed -n 's/^\[env:\(.*\)\]$/\1/p' platformio.ini | grep -v '^native$')
fi

# esptool 4.x from PlatformIO's own package (esptool 5 renamed merge_bin).
ESPTOOL_PY="$CORE_DIR/packages/tool-esptoolpy/esptool.py"
PIO_PYTHON="$CORE_DIR/penv/bin/python"
[ -x "$PIO_PYTHON" ] || PIO_PYTHON="python3"
if [ ! -f "$ESPTOOL_PY" ]; then
	echo "esptool not found at $ESPTOOL_PY (build once with pio first)." >&2
	exit 1
fi

BOOT_APP0="$(find "$CORE_DIR/packages/framework-arduinoespressif32/tools/partitions" \
	-name boot_app0.bin 2>/dev/null | head -1)"
if [ -z "$BOOT_APP0" ]; then
	echo "boot_app0.bin not found in PlatformIO packages." >&2
	exit 1
fi

mkdir -p dist
for env in "${ENVS[@]}"; do
	echo "[package] building $env"
	pio run -e "$env"
	d=".pio/build/${env}"
	out="dist/buttons-m17-${env}-${VERSION}.bin"
	# Offsets match partitions_16mb_wio.csv: otadata (boot_app0) at 0xE000,
	# app0 at 0x10000. Flash mode dio in the image header, as camillia-mt
	# ships; the 2nd-stage bootloader switches to qio itself.
	"$PIO_PYTHON" "$ESPTOOL_PY" --chip esp32s3 merge_bin \
		-o "$out" -fm dio -ff 80m -fs 16MB \
		0x0     "$d/bootloader.bin" \
		0x8000  "$d/partitions.bin" \
		0xe000  "$BOOT_APP0" \
		0x10000 "$d/firmware.bin"
	cp "$d/firmware.bin" "dist/buttons-m17-${env}-${VERSION}-ota.bin"
	cp "$d/firmware.elf" "dist/buttons-m17-${env}-${VERSION}.elf"
	echo "[package] $out"
done
