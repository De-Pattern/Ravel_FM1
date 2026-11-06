# Where Ravel trades sound quality or features for CPU and memory

A list of everything made cheaper to fit the FM-1, so it can be undone when there is room again (or made cheaper
still). Each entry says what changed, what it costs in sound, and where to change it back. "Free" means the sound
did not change (checked sample for sample, or within the variation of a random patch).

## Memory: what was moved or removed to make room

| What | Why | Undo / where |
| --- | --- | --- |
| The hidden game (MOOM) removed (v1.16) | about 48 KB of flash and 4.7 KB of RAM: room for WEFT code in RAM and for the screen reader | `git show v1.15.4:firmware/app/rv_moom.c`, its sound effects in `rv_engine.c` and its hooks |
| The interface's state (`ui`), the event queue (`evq`), the take buffer (`take_pat`) and the looper's table (`lm_dec`) in plain RAM, not the pool (v1.15.1, v1.16) | the pool keeps room for render code (RV_HOT2) | `RV_RAMBSS` on each; no effect on sound or speed (the same RAM) |
| HYBRID FM's formant code, WEFT's bigger blocks (six-mode bodies, CHEBY, FORMANT, VOCODER, SHIFT, DIFFUSE, STRING, the switches) run from RAM (`RV_HOT2`) | speed (flash is slow to run from) | free |
| WEFT's main voice loop (`weft_voice`, about 15 KB) runs from the pool (v1.17). To make room: the screen is sent in 8-row pieces, not 16 (half the swap buffers, 7.5 KB of RAM; an unchanged half-band is not sent); WEFT's delay lines (8 KB) moved from the pool to that RAM; the tracks' RING carrier is summed in the chain instead of kept per track (3.5 KB); the pool's spare room for later code went from 8 KB to 6 KB | free in sound; the screen may draw a hair slower on a full redraw | `BLIT_ROWS` in `plat_fm1.c`; `wdly` in `rv_weft.c`; `RV_HOT2` on `weft_voice`; the headroom check in `tools/build_fm1.py` |

## Sound: what was made cheaper

| What | Cost in sound | Undo / where |
| --- | --- | --- |
| Wavetables read from a 64-sample copy (v1.14) | about −36 dB of difference; the highest harmonics of the brightest waves a little softer; about 10 KB of flash saved | `tools/gen_wavetables.py`, `wtab_read` in `rv_synth.c` (use `wt_tab` again) |
| WEFT: slow CYCLE (< 50 Hz) and LISSA (< 30 Hz) worked out every 8 samples, smoothed (v1.14) | an LFO's corners rounded by 8 samples (0.2 ms); none audible | the control-rate mask in `weft_voice` (`rv_weft.c`) |
| WEFT: blocks nothing heard depends on don't run; noise made only when read; scopes only while shown; filter-bank and RING coefficients cached per note (v1.14) | free | `rv_weft.c` |
| WEFT SHIFT: the carrier is a rotating pair, not two sines a sample (v1.16) | free (the same shift; the carrier's phase starts where it was) | `wf_rot` in `rv_weft.c` |
| HYBRID FM: the formants and breath at half the sample rate, eased between (v1.15.2) | slightly duller breath; the formants themselves unchanged below ~11 kHz | `hfm_run` in `rv_synth.c` (run every sample; `hfm_svf` without the `+ 1.0f`) |
| HYBRID FM: a cubic soft limit instead of a rational one; breath bands skipped with no AIR (v1.15.1) | formant sounds about 5% louder; none otherwise | `hfm_run` |
| WEFT: patches with more than eight blocks play one voice | polyphony, until the cost on the FM-1 is measured | the voice limit in `weft_voice_start` |
| GLO › CPU: the overload guard fades voices near the limit | voices dropped instead of crackles | `rv_engine.c` |
| The guard's relief (EASED 1–3, v1.17): past fewer voices, the delay's new grains halved, then quartered (six, four, two at most) and WEFT kept to one voice; back step by step once calm | thinner grains and fewer WEFT voices, only while overloaded | `guard_tick` in `rv_engine.c`, `gmax`/`grate` in `delay_run` (`rv_bus.c`) |
| WEFT RATE HALF (v1.18, off by default) | the patch at 22 kHz: the top octave gone, a little grit; about half the time | the RATE knob (WEFT RATE); `weft_voice_one` in `rv_weft.c` |
| The tracks' mix (`mix_track`), a plain chain (`chain_plain`), the step's clears and copies (`rv_fill0`, `rv_copyf`, unrolled by 4) in RAMTEXT (v1.21) | bit for bit the same; RAMTEXT 29,576 of 29,720 B | core 1's audio 58.5 % to 54.6 % of its time in fm1-emu's FM1_STRICT model (`docs/fm1-emu.md`) |
| The clock (`clock_tick`, `clock_wave`), CUTS (`cuts_run`), the buses' sum (`bus_sum`, one loop) and `buf_fix` in RAMTEXT; `master_tail` keeps its state in locals; `eff_compute` in the pool instead (v1.22) | bit for bit the same; RAMTEXT 28,644 of 29,720 B; the pool's required headroom 2 KB, was 6 KB (`tools/build_fm1.py`) | core 1's audio 53.5 % to 50.6 % of its time in fm1-emu's FM1_STRICT model |
| The mix skips a send at 0 and the LOOP capture without a LOOP track; the step's clears, excitation sums (`rv_addmul`) and loudness (`rv_abs_sum`) in RAMTEXT; `clock_wave` works out only the wave in use (v1.22.1) | bit for bit the same (a send at 0 left a zero's sign alone, nothing heard); RAMTEXT 28,948 of 29,720 B | the step 51.3 % to 50.0 % in fm1-emu's FM1_STRICT model |
| BUFFER SAFE (v1.20): a block rendered ahead (`sbuf`, main_fm1.c) | +11.6 ms of latency (TIGHT: the old way, GLO › CPU 2); the guard then reads 1024 frames and allows 90 % (`GUARD_HI`, `GUARD_WIN` in `rv_engine.c`) | 4 KB of RAM, found by sending the screen in 4-row bands (`BLIT_ROWS`, plat_fm1.c) |
| SPACE's MOTION once a step, a straight line between (v1.20) | about 55 dB under the signal (the 12-bit grain); `halfrate_run` in `rv_bus.c`, the `d[]`/`dd[]` lines | about 18 % less work, more on the FM-1 (two combs at a time) |
| WEFT's plan structure cached (`wps`, two a core), loop breakers (a DELAY ≥ 18 samples, no B: `brk`) (v1.20) | bit for bit the same (`tools/weft_compare.py`) | each core its own cache, since both plan at once |
| `artic_cfg_track` in the flash (v1.20) | once a voice a step; no measurable cost | room in RAMTEXT for SPACE |
| The BED rests when nothing plays (v1.19): sequencer stopped and every track silent for 4 s | it fades out, and back in with the next sound or PLAY (no drone left running on its own) | `BED_REST`/`bo_bed` in `rv_engine.c` |
| SKIP SILENT (v1.17): a track muted or at LEVEL 0 for half a second, nothing else using it, stops its voices | a note held across an unmute doesn't come back (the next one plays) | `skip_mask` in `rv_engine.c`; GLO › CPU, SILENT PLAY |

## Scheduling (no change to the sound)

| What | Effect |
| --- | --- |
| WEFT's blocks run 16 samples at a time where the cords allow; loops sample by sample (v1.18) | sample for sample the same; a patch without loops about half the work. `weft_plan` (the order, in the flash), `weft_voice` and the `wk_*` kernels (in the pool); some little-used kernels (`WK_FLASH`) run from the flash |
| The buses as three jobs (BED with SPACE, FEEDBACK, DELAY), each on whichever core has room; outputs summed on the first core in the old order (v1.19) | bit for bit the same, one core or two (`bus_jobs`, `c2_share` in `rv_engine.c`; `RV_BU_FORCE` builds a test that shares them every way in turn) |
| SPACE's tank keeps its lines' positions in registers and reads whole-sample delays without interpolating; DELAY skips WOW and FLUTTER at 0 (v1.19) | sample for sample the same; SPACE about 18 % less work (43 % with MOTION 0), DELAY about 25 % less |
| Tracks shared whole between the cores, their effects (chain_a) with their voices (v1.18) | one core: bit for bit the same; two: the mix adds up in the same order |
| The two cores share the voices by their measured cost, not every other voice (v1.16.2) | the cores finish together; a heavy patch no longer piles onto the first core |

## Ideas not done yet (more CPU, if it is needed)

- The tracks' chains (the per-sample loop in `render_sub`, about 34 KB with the rest of it) still run from flash; splitting the per-sample part out into RAM would need more room again.
- WEFT loops through a DELAY or STRING could run a run at a time too (their own delay is longer than a run).
- HYBRID FM: two formants instead of three.
- A voice limit for HYBRID FM with formants on.
