#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# Fetch the three files of JieLi's AC79 SDK (Apache-2.0) that a firmware package contains:
# cpu/wl82/tools/uboot.boot, cfg_tool.bin and cfg/eq_cfg_hw.bin, at the tested version.
#   tools/get_ac79_files.sh [DEST]      (default: ~/fw-AC79_AIoT_SDK); then AC79_SDK=DEST
set -e
DEST="${1:-$HOME/fw-AC79_AIoT_SDK}"
if [ ! -f "$DEST/cpu/wl82/tools/uboot.boot" ]; then
    rm -rf "$DEST"
    git clone -q --depth 1 --filter=blob:none --sparse --branch AC79NN_SDK_V1.2.1_2023-12-13 \
        https://gitee.com/Jieli-Tech/fw-AC79_AIoT_SDK.git "$DEST"
    git -C "$DEST" sparse-checkout set cpu/wl82/tools
fi
echo "AC79_SDK=$DEST"
