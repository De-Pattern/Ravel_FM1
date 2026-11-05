#!/usr/bin/env python3
"""WEFT, bit for bit: build the simulator at two commits with every WEFT voice's sound folded into one number
(RV_WEFT_HASH), play every factory patch three ways and N random patches on both, and list where they differ.
A change meant to leave the sound alone (a faster engine, a reordering) should show none. (Both commits need
the RV_WEFT_HASH build: from 1.18.0 on.)

  tools/weft_compare.py BASE_REF [NEW_REF|.] [N]      (. : the working tree; N random patches, 300 by default)
"""
import os, random, shutil, subprocess, sys, tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
WO_N, WS_N, AUTO = 56, 30, 13


def build(ref, out, tmp):
    src = ROOT if ref == "." else tmp / ("tree-" + ref.replace("/", "_"))
    if ref != ".":
        subprocess.run(["git", "worktree", "add", "-f", "--detach", str(src), ref], cwd=ROOT, check=True, capture_output=True)
    (src / "build" / "gen").mkdir(parents=True, exist_ok=True)
    if not (src / "build" / "gen" / "rv_font.h").exists():
        subprocess.run([sys.executable, str(src / "tools" / "gen_rv_font.py"), str(src / "build" / "gen" / "rv_font.h")], cwd=src, check=True, capture_output=True)
    subprocess.run(["cc", "-O2", "-ffp-contract=off", "-std=gnu99", "-DRV_WEFT_HASH", "-DRV_PROF_HOT", "-Wno-unused-function",
                    "-Ifirmware/app", "-Ibuild/gen", "-o", str(out), "host/ravel_host.c", "firmware/app/ravel_app.c", "-lm"],
                   cwd=src, check=True)
    return src


def play(L, rnd, long=False):
    for _ in range(rnd.randint(2, 5)):
        keys = rnd.sample([10, 12, 14, 15, 17, 19, 21, 22, 24], rnd.randint(1, 2))
        L += [f"key {k} down" for k in keys]
        L.append(f"wait {rnd.randint(20, 400)}")
        if rnd.random() < 0.3:
            L.append(f"wset 1 7 {rnd.randint(1, 24)} {rnd.randint(0, 127)}")
            L.append(f"wait {rnd.randint(5, 80)}")
        L += [f"key {k} up" for k in keys]
        L.append(f"wait {rnd.randint(10, 900 if long else 300)}")


def scenarios(d, n):
    for p in range(28):
        for var in range(3):
            rnd = random.Random(p * 10 + var)
            L = ["wait 100", "kind 1 6", f"wpreset 1 {p}"]
            L += [[], ["settp 1 VOICES 1"], ["wset 1 10 0 1"]][var]
            L.append("wait 30")
            play(L, rnd, True)
            (d / f"p{p:02d}_{var}.rvl").write_text("\n".join(L) + "\n")
    for r in range(n):
        rnd = random.Random(1000 + r)
        L = ["wait 100", "kind 1 6"]
        nb = rnd.randint(2, 12)
        for b in range(1, 13):
            ty = rnd.randint(1, WO_N - 1) if b <= nb and rnd.random() >= 0.15 else 0
            src = lambda: 0 if (x := rnd.random()) < 0.15 else rnd.randint(1, 12) if x < 0.65 else rnd.randint(13, WS_N - 1)
            L += [f"wset 1 0 {b} {ty}", f"wset 1 1 {b} {src()}", f"wset 1 2 {b} {src()}",
                  f"wset 1 3 {b} {rnd.randint(-100, 100)}", f"wset 1 4 {b} {rnd.randint(-100, 100)}"]
            if rnd.random() < 0.7:
                L += [f"wset 1 7 {2 * b - 1} {rnd.randint(0, 127)}", f"wset 1 7 {2 * b} {rnd.randint(0, 127)}"]
            if rnd.random() < 0.15:
                L.append(f"wset 1 9 {b} 1")
        L.append(f"wset 1 5 0 {rnd.choice([AUTO, AUTO, rnd.randint(0, 12)])}")
        if rnd.random() < 0.2:
            L.append("wset 1 10 0 1")
        if rnd.random() < 0.3:
            L.append("settp 1 VOICES 1")
        L.append("wait 20")
        play(L, rnd)
        (d / f"r{r:03d}.rvl").write_text("\n".join(L) + "\n")


def main():
    base, new = sys.argv[1], sys.argv[2] if len(sys.argv) > 2 else "."
    n = int(sys.argv[3]) if len(sys.argv) > 3 else 300
    tmp = Path(tempfile.mkdtemp(prefix="weftcmp-"))
    trees = []
    try:
        trees.append(build(base, tmp / "a", tmp))
        trees.append(build(new, tmp / "b", tmp))
        d = tmp / "sc"
        d.mkdir()
        scenarios(d, n)
        bad = 0
        files = sorted(d.glob("*.rvl"))
        for f in files:
            h = [next((l for l in subprocess.run([str(tmp / x), f.name], cwd=d, capture_output=True, text=True).stdout.splitlines()
                       if l.startswith("wefthash")), "none") for x in ("a", "b")]
            if h[0] != h[1]:
                bad += 1
                print(f"DIFF {f.name}: {h[0]} | {h[1]}")
        print(f"{bad} of {len(files)} differ")
        return 1 if bad else 0
    finally:
        for t in trees:
            if t != ROOT:
                subprocess.run(["git", "worktree", "remove", "--force", str(t)], cwd=ROOT, capture_output=True)
        shutil.rmtree(tmp, ignore_errors=True)


if __name__ == "__main__":
    sys.exit(main())
