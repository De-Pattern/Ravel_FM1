# Ravel in fm1-emu

[fm1-emu](https://github.com/joakimeriksson/fm1-emu) (Joakim Eriksson, MIT) emulates the FM-1 itself: its two
pi32v2 cores, flash, screen, keys, knobs and audio, so the `Ravel-FM1.fwsc` a release ships runs in it as it
is. Ravel uses it for two things.

## The release check

`tools/emu_check.sh build/fm1/Ravel-FM1.fwsc [EMU_DIR]` (run by the release workflow before a package goes out)
plays a chord on T1 after the start screen, three ways:

1. as the FM-1 would (the emulator's fast mode): it boots, plays, and is still playing 12 seconds in;
2. with a flaky second core (`--flaky-core1 1e-5`: core 2 misreads RAM now and then, as some units do): Ravel
   falls back to one core and plays on;
3. with the FM-1's measured instruction timing (`FM1_STRICT=1`, 360 MHz): it still plays.

A halt or silence at the end fails the release. The emulator is pinned (`FM1_EMU_REV`) so a change on its side
cannot fail a release by surprise.

## Profiling with the FM-1's own costs

`FM1_STRICT=1` runs the emulator with each instruction's cost as measured on a real FM-1 (from fm1-dock's probes:
an ALU op ~0.6 cycles, an FPU add or multiply 1.4, a load 1.25, a store 1.4, a taken branch ~5, a call ~5) and
the flash cache (32 KB, 64-byte lines, shared by the cores) with ~300 cycles a miss. `FM1_TPROF` records where
each core's time went. It is a model, not the chip; the CPU pages' numbers on a real FM-1 are the truth, but
it ranks the work the right way, which the simulator on a PC (x86 instructions) cannot.

```sh
# a bench build: DEMO 1, factory WEFT patch K on T1 (-1: none), playing; RAVEL_DEBUG=1 adds line numbers
RAVEL_BENCH=7 RAVEL_DEBUG=1 python3 tools/build_fm1.py
mkdir -p run/work && cd run
FM1_STRICT=1 FM1_BENCH_KEYS='9000:C4@100,9000:E4@100,9000:G4@100' FM1_TPROF=../tp.txt,10000,13000 \
    fm1-live --fwsc ../build/fm1/Ravel-FM1.fwsc --mhz 360 --midi none --shot 13500
cd .. && python3 tools/emu_prof.py tp.txt build/fm1/ravel.elf            # by function, each core
python3 tools/emu_prof.py tp.txt build/fm1/ravel.elf --lines render_sub  # one function, by source line
```

`RAVEL_BENCH_SLOWUI=1` also draws the screen once a second, to see what the drawing costs the audio. A bench
build is for the emulator only: don't install it.

## What it found (v1.21)

With TANGLE on T1 and DEMO 1 playing, before v1.21 the render's glue in the flash (`render_sub`) was core 1's
largest single cost (18 % of its time), most of it the per-sample mix of the tracks into the main mix and the
sends, and each track's chain (`chain_a`) another 5 %, both running from the flash. They are in RAM now (see
CHANGELOG 1.21.0): core 1's audio work went from 58.5 % to 54.6 % of its time, core 2's from 38.6 % to 34.7 %,
the sound the same, sample for sample. Drawing the screen once a second instead of 15-30 times changed the
audio's share by nothing measurable: in the model, the screen's code passing through the shared cache is not
what holds the audio up. Core 2 was idle about 60 % of the time.
