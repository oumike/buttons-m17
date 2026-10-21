# shellcheck shell=bash
# Where PlatformIO keeps its toolchains, frameworks and libraries (its "core
# dir"). Sourced by the build scripts; not run on its own. Same scheme as
# camillia-mt.
#
# PlatformIO's default is ~/.platformio. A checkout can keep its own instead
# with a platformio_local.ini (gitignored, so it is one machine's choice):
#
#     [platformio]
#     core_dir = /absolute/path/to/buttons-m17/.pio-core
#
# platformio.ini pulls that file in, so a plain `pio run` or the IDE uses it
# too. PLATFORMIO_CORE_DIR still overrides it.
#
# Bash 3.2 (macOS's stock bash).

# Prints the core dir. $1 is the checkout's root.
pio_default_core_dir() {
    local root="$1" local_ini dir
    if [[ -n "${PLATFORMIO_CORE_DIR:-}" ]]; then
        echo "$PLATFORMIO_CORE_DIR"
        return
    fi
    local_ini="$root/platformio_local.ini"
    if [[ -f "$local_ini" ]]; then
        dir=$(sed -n 's/^[[:space:]]*core_dir[[:space:]]*=[[:space:]]*//p' "$local_ini" \
            | tail -1 | sed 's/[[:space:]]*$//')
        if [[ -n "$dir" ]]; then
            case "$dir" in
                "~"*) dir="$HOME${dir#\~}" ;;
                /*)   ;;
                *)    dir="$root/$dir" ;;
            esac
            echo "$dir"
            return
        fi
    fi
    echo "$HOME/.platformio"
}
