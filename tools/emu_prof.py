#!/usr/bin/env python3
"""Where the FM-1's time goes, from fm1-emu's profile (github.com/joakimeriksson/fm1-emu).

  emu_prof.py PROFILE ELF [--top N] [--lines FUNCTION]

PROFILE: fm1-live's FM1_TPROF output (run with FM1_STRICT=1 for its measured cycle costs): lines
"CORE PC WEIGHT" (the time each core spent at PC) and "mCORE LINE COUNT" (the flash cache's misses
by line). ELF: build/fm1/ravel.elf of the same build. Prints, per core, the functions by their share
of the core's time, where each runs from (RAMTEXT, the pool, the flash), and the flash cache's misses
each caused; then the regions' totals. (An instruction count is not a cycle count: run FM1_STRICT=1.)
--lines FUNCTION: that function's time and misses by source line instead (a build with RAVEL_DEBUG=1).
"""
import os
import re
import subprocess
import sys
from bisect import bisect_right
from collections import defaultdict

TOOLS = os.environ.get("JIELI_TOOLCHAIN", "/root/.jieli/jieli-linux-toolchains-20260730.1")


def region(a):
    if a < 0x01C08000:
        return "RAMTEXT"
    if a < 0x01C20000:
        return "RAM"
    if a < 0x01C80000:
        return "POOL"
    if 0x02000000 <= a < 0x04000000:
        return "FLASH"
    return "?"


def symbols(elf):
    nm = os.path.join(TOOLS, "pi32v2", "bin", "nm")
    out = subprocess.run([nm, "-S", "-n", "-C", elf], capture_output=True, text=True).stdout
    syms = []
    for l in out.splitlines():
        p = l.split()
        if len(p) >= 4 and p[2].lower() in ("t", "w"):
            syms.append((int(p[0], 16), int(p[1], 16), p[3]))
    syms.sort()
    return syms


def line_map(elf, lo, hi):
    """address -> "file:line", from the disassembly with line numbers (a -g build)"""
    od = os.path.join(TOOLS, "pi32v2", "bin", "objdump")
    out = subprocess.run([od, "-d", "-l", f"--start-address={lo:#x}", f"--stop-address={hi:#x}", elf],
                         capture_output=True, text=True).stdout
    cur, m = "?", {}
    for l in out.splitlines():
        mm = re.match(r"^(/\S+|\S+\.[ch]):(\d+)", l)
        if mm:
            cur = f"{os.path.basename(mm.group(1))}:{mm.group(2)}"
            continue
        mm = re.match(r"^\s*([0-9a-f]+):", l)
        if mm:
            m[int(mm.group(1), 16)] = cur
    return m


def main():
    args = sys.argv[1:]
    top = 30
    lines = None
    if "--lines" in args:
        i = args.index("--lines")
        lines = args[i + 1]
        del args[i:i + 2]
    if "--top" in args:
        i = args.index("--top")
        top = int(args[i + 1])
        del args[i:i + 2]
    prof, elf = args
    syms = symbols(elf)
    starts = [s[0] for s in syms]

    def fn(a):
        i = bisect_right(starts, a) - 1
        if i >= 0 and a < syms[i][0] + max(syms[i][1], 2):
            return syms[i][2]
        return f"?{a:08x}"

    if lines:
        ss = [x for x in syms if x[2] == lines]
        if not ss:
            sys.exit(f"no function {lines}")
        lo, size = ss[0][0], ss[0][1]
        lm = line_map(elf, lo, lo + size)
        keys = sorted(lm)
        lt = defaultdict(lambda: defaultdict(int))
        tot = defaultdict(int)
        for l in open(prof):
            p = l.split()
            if len(p) != 3:
                continue
            core, a, w = p[0], int(p[1], 16), int(p[2])
            if not core.startswith("m"):
                tot[core] += w
            if not (lo <= a < lo + size):
                continue
            i = bisect_right(keys, a) - 1
            ln = lm[keys[i]] if i >= 0 else "?"
            lt[core][ln] += w
        for core in sorted(lt):
            label = f"core {int(core) + 1}" if not core.startswith("m") else f"flash cache misses, core {int(core[1:]) + 1}"
            all_ = sum(lt[core].values()) or 1
            print(f"== {lines}, {label}: {all_}" + (f" ({100.0 * all_ / (tot[core] or 1):.1f}% of the core)" if not core.startswith("m") else ""))
            for ln, w in sorted(lt[core].items(), key=lambda x: -x[1])[:top]:
                print(f"  {100.0 * w / all_:5.1f}%  {ln}")
        return
    t = defaultdict(lambda: defaultdict(int))      # core -> fn -> weight
    reg = defaultdict(lambda: defaultdict(int))    # core -> region -> weight
    miss = defaultdict(lambda: defaultdict(int))   # core -> fn -> misses
    where = {}
    for l in open(prof):
        p = l.split()
        if len(p) != 3:
            continue
        core, a, w = p[0], int(p[1], 16), int(p[2])
        f = fn(a)
        if core.startswith("m"):
            miss[core[1:]][f] += w
            continue
        t[core][f] += w
        reg[core][region(a)] += w
        where[f] = region(a)
    for core in sorted(t):
        tot = sum(t[core].values()) or 1
        mt = sum(miss[core].values())
        print(f"== core {int(core) + 1}: {tot} (time units); flash cache misses {mt}")
        print("   " + "  ".join(f"{r} {100.0 * w / tot:.1f}%" for r, w in sorted(reg[core].items(), key=lambda x: -x[1])))
        for f, w in sorted(t[core].items(), key=lambda x: -x[1])[:top]:
            print(f"  {100.0 * w / tot:5.1f}%  {where.get(f, '?'):7s}  misses {miss[core].get(f, 0):6d}  {f}")
        rest = [(f, m) for f, m in miss[core].items() if f not in dict(sorted(t[core].items(), key=lambda x: -x[1])[:top])]
        if rest:
            print("   the most flash cache misses elsewhere: " + ", ".join(f"{f} {m}" for f, m in sorted(rest, key=lambda x: -x[1])[:8]))


if __name__ == "__main__":
    main()
