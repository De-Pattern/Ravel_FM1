#!/bin/sh
# The FM-1 package run in fm1-emu (github.com/joakimeriksson/fm1-emu), an emulator of the FM-1 that runs its
# firmware as it is: the release's boot-and-play check, before a package goes out.
#
#   tools/emu_check.sh build/fm1/Ravel-FM1.fwsc [EMU_DIR]
#
# EMU_DIR: an fm1-emu checkout (cloned at FM1_EMU_REV into build/fm1-emu if not given). Three runs, each
# a chord held on T1 after the start screen:
#   1. it boots, plays, and is still playing at the end (sound in the last second, no halt)
#   2. the same with a flaky second core (--flaky-core1): it falls back to one core and plays on
#   3. with the FM-1's measured instruction timing (FM1_STRICT=1, 360 MHz): it still plays
set -eu
FWSC=$(realpath "$1")
EMU=${2:-build/fm1-emu}
REV=${FM1_EMU_REV:-9fa0215}
if [ ! -d "$EMU/.git" ]; then
    git clone -q https://github.com/joakimeriksson/fm1-emu.git "$EMU"
    git -C "$EMU" checkout -q "$REV"
fi
(cd "$EMU" && cargo build -q --release -p fm1-live)
LIVE=$(realpath "$EMU/target/release/fm1-live")
OUT=$(realpath -m build/emucheck)
mkdir -p "$OUT/work"
KEYS='9000:C4@100,9000:E4@100,9000:G4@100'
fail=0

peak_last() {   # the loudest sample in the last second of a raw 44.1 kHz stereo s16le file
    python3 - "$1" <<'EOF'
import struct, sys
d = open(sys.argv[1], "rb").read()
a = struct.unpack("<%dh" % (len(d) // 2), d)
print(max([abs(x) for x in a[-88200:]] or [0]))
EOF
}

run() {   # NAME [fm1-live options...]
    name=$1; shift
    log="$OUT/$name.log"
    (cd "$OUT" && FM1_BENCH_KEYS="$KEYS" FM1_BENCH_WAV="$OUT/$name.raw" \
        "$LIVE" --fwsc "$FWSC" --mhz 360 --midi none "$@" --bench 12000) > "$log" 2>&1 || true
    pk=$(peak_last "$OUT/$name.raw" 2>/dev/null || echo 0)
    if grep -q "^halt" "$log"; then
        echo "FAIL $name: the emulated FM-1 halted"; sed -n '/^halt/,$p' "$log" | head -5; fail=1
    elif [ "$pk" -lt 100 ]; then
        echo "FAIL $name: silent at the end (peak $pk)"; tail -5 "$log"; fail=1
    else
        echo "ok   $name: plays (peak $pk at the end); $(grep -o '[0-9.]*x real time' "$log" | tail -1)"
    fi
}

run boot
run flaky --flaky-core1 1e-5 --seed 7     # a second core that misreads RAM: Ravel falls back to one and plays on
# the FM-1's timing: interpreted, slower; a shorter run, the panel's audio peak
(cd "$OUT" && FM1_STRICT=1 FM1_BENCH_KEYS="$KEYS" "$LIVE" --fwsc "$FWSC" --mhz 360 --midi none --shot 11000) > "$OUT/strict.log" 2>&1 || true
pk=$(sed -n 's/.*audio peak over the last 0.5 s: \([0-9]*\).*/\1/p' "$OUT/strict.log" | tail -1)
if grep -q "^halt" "$OUT/strict.log" || [ "${pk:-0}" -lt 100 ]; then
    echo "FAIL strict: with the FM-1's timing it stopped (peak ${pk:-0})"; tail -5 "$OUT/strict.log"; fail=1
else
    echo "ok   strict: with the FM-1's timing it plays (peak $pk)"
fi
exit $fail
