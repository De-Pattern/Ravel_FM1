#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# Build the Ravel simulator (the whole instrument on this machine) into build/host/ravel_host.
# The same sources and Beads options as the firmware; -ffp-contract=off like the FM-1 (no fused multiply-add).
set -e
cd "$(dirname "$0")/.."
CC="${CC:-cc}"
mkdir -p build/gen build/host
[ -f build/gen/rv_font.h ] || python3 tools/gen_rv_font.py build/gen/rv_font.h >/dev/null
$CC -O2 -ffp-contract=off -std=gnu99 -Wall -Wextra -Wno-unused-function -Wno-unused-parameter \
    -DBD_NUM_GRAINS=16 -DBD_NO_DELAY -DBD_READER_ONLY -Ifirmware/app -Ibuild/gen \
    -o build/host/ravel_host host/ravel_host.c firmware/app/ravel_app.c core/beads/bd.c core/beads/bd_tables.c -lm
echo "host: build/host/ravel_host"
