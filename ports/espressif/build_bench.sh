#!/usr/bin/env bash
# Clean-rebuild the two bench devkit firmwares with CIRCUITPY_CONSOLE_UART on.
# Kept as a file rather than an inline command: `wsl -- bash -lc '...'` mangles
# $VAR and $(...) before the WSL shell ever sees them.
set -uo pipefail

cd "$HOME/dev-projects/python/circuitpython/cp-wifi-ap-debug/ports/espressif" || exit 1

. ./esp-idf/export.sh > /tmp/idf_export.log 2>&1
echo "IDF version: $(idf.py --version 2>&1 | tail -1)"
echo

for BOARD in espressif_esp32s3_devkitc_1_n8 espressif_esp32c6_devkitc_1_n8; do
    echo "########## $BOARD ##########"

    # The existing build dirs carry stale genhdr/qstrdefs, which is what broke
    # ulab; clean is required after changing mpconfigboard.{h,mk}.
    make BOARD="$BOARD" clean > "/tmp/clean_$BOARD.log" 2>&1
    echo "clean rc=$?"

    make BOARD="$BOARD" \
        EXTRA_SDKCONFIG=esp-idf-config/sdkconfig-wifidebug-opt.defaults \
        -j4 > "/tmp/build_$BOARD.log" 2>&1
    RC=$?
    if [ "$RC" -eq 0 ]; then
        echo "BUILD_OK $BOARD"
        ls -l "build-$BOARD"/firmware.bin "build-$BOARD"/firmware.uf2 2>/dev/null
        grep -iE "console.?uart" "/tmp/build_$BOARD.log" | head -3
    else
        echo "BUILD_FAIL $BOARD rc=$RC"
        grep -m5 -nE "error:|Error [0-9]" "/tmp/build_$BOARD.log"
    fi
    echo
done
