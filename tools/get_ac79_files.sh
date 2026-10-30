#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# Fetch the three files of JieLi's AC79 SDK (Apache-2.0) that a firmware package contains:
# cpu/wl82/tools/uboot.boot, cfg_tool.bin and cfg/eq_cfg_hw.bin, at the tested version.
#   tools/get_ac79_files.sh [DEST]      (default: ~/fw-AC79_AIoT_SDK); then AC79_SDK=DEST
set -e
DEST="${1:-$HOME/fw-AC79_AIoT_SDK}"
# (gitee.com is often slow or drops the connection from abroad: up to five tries, a while apart)
try=1
while [ ! -f "$DEST/cpu/wl82/tools/uboot.boot" ]; do
    rm -rf "$DEST"
    if git clone -q --depth 1 --filter=blob:none --sparse --branch AC79NN_SDK_V1.2.1_2023-12-13 \
            https://gitee.com/Jieli-Tech/fw-AC79_AIoT_SDK.git "$DEST" &&
        git -C "$DEST" sparse-checkout set cpu/wl82/tools; then
        break
    fi
    if [ "$try" -ge 5 ]; then
        echo "get_ac79_files: gitee.com could not be reached after $try tries" >&2
        exit 1
    fi
    echo "get_ac79_files: try $try failed, again in $((try * 20)) s" >&2
    sleep $((try * 20))
    try=$((try + 1))
done
echo "AC79_SDK=$DEST"
