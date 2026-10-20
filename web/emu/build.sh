#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# Build the browser emulator: web/emu/ravel.wasm, from the same sources as the firmware.
# Needs clang with the wasm32 target and wasm-ld (no emscripten, no C library).
set -e
cd "$(dirname "$0")/../.."
CC="${CC:-clang}"
mkdir -p build/gen
[ build/gen/rv_font.h -nt tools/gen_rv_font.py ] || python3 tools/gen_rv_font.py build/gen/rv_font.h >/dev/null   # (again when the generator changes)
$CC --target=wasm32 -nostdlib -ffreestanding -O2 -ffp-contract=off -std=gnu99 \
    -Wall -Wno-unused-function -Wno-unused-parameter -Wno-unused-variable -Wno-unused-but-set-variable \
    -DBD_NUM_GRAINS=16 -DBD_NO_DELAY -DBD_READER_ONLY -Ifirmware/app -Ibuild/gen \
    -Wl,--no-entry -Wl,--export-memory -Wl,-z,stack-size=524288 -Wl,--initial-memory=16777216 \
    -o web/emu/ravel.wasm web/emu/ravel_web.c firmware/app/ravel_app.c core/beads/bd.c core/beads/bd_tables.c
echo "web: web/emu/ravel.wasm ($(wc -c < web/emu/ravel.wasm) B)"
