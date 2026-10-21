#!/bin/bash
# Build, upload and monitor buttons-m17 — or just build. Modeled on
# camillia-mt's scripts/build-upload-monitor.sh.
#
# Usage: scripts/build-upload-monitor.sh [device flag] [--erase|-E] [--fullclean|-F]
#                                        [--just-build|-B] [--test|-T]
#
# With no device flag you are prompted to pick one (only the Wio Tracker L2
# exists today, so it is picked for you).

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR/.."

WIO_TRACKER_L2_ENV_NAME="wio-tracker-l2"
NATIVE_ENV_NAME="native"
ENV_NAME=""
ENV_EXPLICIT=false
ERASE_FIRST=false
FULLCLEAN=false
JUST_BUILD=false
RUN_TESTS=false

has_env() {
	grep -q "^\[env:$1\]" platformio.ini
}

# Every firmware [env:NAME] in platformio.ini, in file order. The native test
# env is not firmware and is run by --test instead.
all_envs() {
	sed -n 's/^\[env:\(.*\)\]$/\1/p' platformio.ini | grep -v "^${NATIVE_ENV_NAME}$"
}

env_label() {
	case "$1" in
		"$WIO_TRACKER_L2_ENV_NAME") echo "Seeed Wio Tracker L2" ;;
		*)                          echo "$1" ;;
	esac
}

select_env_or_exit() {
	if has_env "$1"; then
		ENV_NAME="$1"
		ENV_EXPLICIT=true
		return
	fi
	echo "Environment '$1' not found in platformio.ini"
	exit 1
}

prompt_for_device() {
	local options=()
	while IFS= read -r env; do
		[ -n "$env" ] && options+=("$env")
	done < <(all_envs)

	if [ "${#options[@]}" -eq 0 ]; then
		echo "No device environments found in platformio.ini"
		exit 1
	fi
	if [ "${#options[@]}" -eq 1 ] || [ ! -t 0 ]; then
		ENV_NAME="${options[0]}"
		echo "[PIO] Using device env: $ENV_NAME ($(env_label "$ENV_NAME"))"
		return
	fi

	echo "Select device to build for:"
	for i in "${!options[@]}"; do
		printf "  %d) %s (%s)\n" "$((i + 1))" "$(env_label "${options[$i]}")" "${options[$i]}"
	done
	while true; do
		read -r -p "Enter choice [1-${#options[@]}]: " choice
		if [[ "$choice" =~ ^[0-9]+$ ]] && [ "$choice" -ge 1 ] && [ "$choice" -le "${#options[@]}" ]; then
			ENV_NAME="${options[$((choice - 1))]}"
			echo "[PIO] Selected device env: $ENV_NAME"
			return
		fi
		echo "Invalid selection. Please choose a number between 1 and ${#options[@]}."
	done
}

show_usage() {
	echo "Usage: $0 [--wio-tracker-l2|-W] [--erase|-E] [--fullclean|-F] [--just-build|-B] [--test|-T]"
	echo "  --wio-tracker-l2, -W  Use Seeed Wio Tracker L2 environment ($WIO_TRACKER_L2_ENV_NAME)"
	echo "                If no device flag is given, you'll be prompted to choose."
	echo "  --erase, -E   Erase flash before upload (wipes settings: Wi-Fi, callsign, password)"
	echo "  --fullclean, -F  Run PlatformIO fullclean before building"
	echo "  --just-build, -B  Compile only - no upload, no monitor, no device needed."
	echo "                Builds every device environment, or just the one named by a"
	echo "                device flag. Keeps going after a failure, prints a summary,"
	echo "                exits non-zero if any environment failed."
	echo "  --test, -T    Run the host unit tests (pio test -e native) first; stops on failure."
}

run_pio_target() {
	echo "[PIO] $2 ($ENV_NAME)..."
	pio run -e "$ENV_NAME" -t "$1"
}

format_duration() {
	local total="$1"
	local h=$((total / 3600)) m=$(((total % 3600) / 60)) s=$((total % 60))
	if [ "$h" -gt 0 ]; then
		printf "%dh %02dm %02ds" "$h" "$m" "$s"
	else
		printf "%dm %02ds" "$m" "$s"
	fi
}

if ! command -v pio >/dev/null 2>&1; then
	echo "PlatformIO CLI not found: install it or run from an environment that provides 'pio'."
	exit 1
fi

for arg in "$@"; do
	case "$arg" in
		--wio-tracker-l2|-W) select_env_or_exit "$WIO_TRACKER_L2_ENV_NAME" ;;
		--erase|-E)          ERASE_FIRST=true ;;
		--fullclean|-F)      FULLCLEAN=true ;;
		--just-build|-B)     JUST_BUILD=true ;;
		--test|-T)           RUN_TESTS=true ;;
		--help|-h)           show_usage; exit 0 ;;
		*)
			echo "Unknown argument: $arg"
			show_usage
			exit 1
			;;
	esac
done

if [ "$RUN_TESTS" = true ]; then
	echo "[PIO] Host unit tests ($NATIVE_ENV_NAME)..."
	pio test -e "$NATIVE_ENV_NAME"
fi

if [ "$JUST_BUILD" = true ]; then
	if [ "$ERASE_FIRST" = true ]; then
		echo "--erase needs a connected device; it cannot be combined with --just-build."
		exit 1
	fi

	BUILD_ENVS=()
	if [ "$ENV_EXPLICIT" = true ]; then
		BUILD_ENVS+=("$ENV_NAME")
	else
		while IFS= read -r env; do
			[ -n "$env" ] && BUILD_ENVS+=("$env")
		done < <(all_envs)
	fi

	echo "[PIO] Build-only sweep over ${#BUILD_ENVS[@]} environment(s)."
	SWEEP_START_TS="$(date +%s)"
	RESULTS=()
	FAILED=0
	for env in "${BUILD_ENVS[@]}"; do
		echo
		echo "===================================================================="
		echo "[PIO] Building $env ($(env_label "$env"))"
		echo "===================================================================="
		ENV_START_TS="$(date +%s)"
		if [ "$FULLCLEAN" = true ] && ! pio run -e "$env" -t fullclean; then
			RESULTS+=("FAIL  $env  (fullclean)  $(format_duration $(($(date +%s) - ENV_START_TS)))")
			FAILED=$((FAILED + 1))
			continue
		fi
		if pio run -e "$env"; then
			SIZE_NOTE=""
			ENV_BIN=".pio/build/${env}/firmware.bin"
			[ -f "$ENV_BIN" ] && SIZE_NOTE="  $(( $(wc -c <"$ENV_BIN") / 1024 )) KB"
			RESULTS+=("ok    $env  $(format_duration $(($(date +%s) - ENV_START_TS)))${SIZE_NOTE}")
		else
			RESULTS+=("FAIL  $env  $(format_duration $(($(date +%s) - ENV_START_TS)))")
			FAILED=$((FAILED + 1))
		fi
	done

	echo
	echo "===================================================================="
	echo "[PIO] Build summary"
	echo "===================================================================="
	for line in "${RESULTS[@]}"; do
		echo "  $line"
	done
	echo
	echo "[PIO] ${#BUILD_ENVS[@]} environment(s), $FAILED failed, total $(format_duration $(($(date +%s) - SWEEP_START_TS)))."
	[ "$FAILED" -gt 0 ] && exit 1
	exit 0
fi

if [ "$ENV_EXPLICIT" = false ]; then
	prompt_for_device
fi

if [ "$ERASE_FIRST" = true ]; then
	run_pio_target "erase" "Erasing device flash"
fi

BUILD_START_TS="$(date +%s)"
if [ "$FULLCLEAN" = true ]; then
	run_pio_target "fullclean" "Full clean"
fi
run_pio_target "upload" "Upload"
echo "[PIO] Build completed in $(format_duration $(($(date +%s) - BUILD_START_TS)))."

ELF_PATH=".pio/build/${ENV_NAME}/firmware.elf"
BIN_PATH=".pio/build/${ENV_NAME}/firmware.bin"
if [ -f "$ELF_PATH" ]; then
	ELF_SHA="$(shasum -a 256 "$ELF_PATH" | awk '{print $1}')"
	echo "[PIO] ELF SHA256: $ELF_SHA"
	echo "[PIO] Runtime monitor should show: ELF file SHA256: ${ELF_SHA:0:16}"
fi
if [ -f "$BIN_PATH" ]; then
	echo "[PIO] BIN SHA256: $(shasum -a 256 "$BIN_PATH" | awk '{print $1}')"
fi

run_pio_target "monitor" "Monitor"
