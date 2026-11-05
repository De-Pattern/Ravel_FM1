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
| SKIP SILENT (v1.17): a track muted or at LEVEL 0 for half a second, nothing else using it, stops its voices | a note held across an unmute doesn't come back (the next one plays) | `skip_mask` in `rv_engine.c`; GLO › CPU, SILENT PLAY |

## Scheduling (no change to the sound)

| What | Effect |
| --- | --- |
| The two cores share the voices by their measured cost, not every other voice (v1.16.2) | the cores finish together; a heavy patch no longer piles onto the first core |

## Ideas not done yet (more CPU, if it is needed)

- The tracks' chains (the per-sample loop in `render_sub`, about 34 KB with the rest of it) still run from flash; splitting the per-sample part out into RAM would need more room again.
- Block-at-a-time WEFT for patches without feedback (about 8–10 KB more code).
- HYBRID FM: two formants instead of three.
- A voice limit for HYBRID FM with formants on.
