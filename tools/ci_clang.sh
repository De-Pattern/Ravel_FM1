#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# clang and wasm-ld for the WebAssembly emulator, on a CI runner: the runner's own when it has them (by name, or a
# versioned clang-NN with its wasm-ld, as Ubuntu installs them), so a build doesn't depend on reaching Ubuntu's
# package servers; apt only when it has neither.
set -e
if command -v clang >/dev/null 2>&1 && command -v wasm-ld >/dev/null 2>&1; then
    exit 0
fi
for d in $(ls -d /usr/lib/llvm-[0-9]* 2>/dev/null | sort -t- -k2 -n -r); do
    if [ -x "$d/bin/clang" ] && [ -x "$d/bin/wasm-ld" ]; then
        sudo ln -sf "$d/bin/clang" /usr/local/bin/clang
        sudo ln -sf "$d/bin/wasm-ld" /usr/local/bin/wasm-ld
        echo "using $d"
        exit 0
    fi
done
sudo apt-get install -y clang lld
