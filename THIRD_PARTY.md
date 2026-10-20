# Third-party code in Ravel

| What | Where | Licence |
| --- | --- | --- |
| Mutable Instruments Beads (grain engine), rewritten in fixed point | `core/beads/` | MIT, `core/beads/LICENSE-beads` |
| Felucca's FM-1 hardware layer: start-up, interrupts, guards, input matrix, LCD, audio codec, flash, USB, second-core hold | `firmware/fm1/hal/`, `firmware/fm1/crt0.S`, `firmware/fm1/app.ld`, `firmware/fm1/src/lcd.c`, `libc.c` | GPL-3.0, by Leo Kuroshita (@kurogedelic), Hügelton Instruments, by way of X0X (charlesvestal/fm1-x0x) |
| Felucca's USB device (MIDI, USB audio to the computer), TRS MIDI, storage, the M-UPGRADE update entry and the update loader | `firmware/fm1/src/usb.c`, `midi_uart.c`, `storage.c`, `ota.c`, `firmware/fm1/loader/` | GPL-3.0, Felucca, by way of X0X |
| Boot, audio interrupt and main loop, panel map and calibration (adapted) | `firmware/fm1/src/main_fm1.c`, `panel.c` | GPL-3.0, Felucca and X0X |
| Build, package and install tools | `tools/build_fm1.py` (adapted), `tools/fm1pkg_make.py`, `tools/lz4blk.py`, `tools/fm1_install.py`, `tools/fm1_rescue.py`, `tools/fm1_rescue.sh`, `tools/get_toolchain.sh` | GPL-3.0, Felucca and X0X |
| Web installer (Web MIDI) | `web/installer/` | GPL-3.0, Felucca and X0X (branded for Ravel) |
| Update-path tests | `tests/fm1/` | GPL-3.0, Felucca and X0X |
| The CZ engine's waveforms, windows, envelope stepping, pitch and level curves and modulation: a port of how MAME's NEC uPD933 device emulates the CZ's sound chip | `firmware/app/rv_synth.c` (the CZ section) | BSD-3-Clause, Copyright Devin Acker (MAME), below |
| Barlow Semi Condensed (screen fonts) | `assets/fonts/` | SIL Open Font License 1.1 |
| PNG writer of the desktop simulator | `host/ravel_host.c` | GPL-3.0 (from X0X) |

Not in this repository, fetched at build time: JieLi's Linux toolchain (`tools/get_toolchain.sh`) and three files
of JieLi's AC79 SDK, `AC79NN_SDK_V1.2.1_2023-12-13` (Apache-2.0): `cpu/wl82/tools/uboot.boot`, `cfg_tool.bin` and
`cfg/eq_cfg_hw.bin`. A firmware package contains those three files, as Felucca's and X0X's packages do.

Ravel uses no M-VAVE code. M-VAVE and FM-1 are trademarks of their owners; Ravel is not affiliated with M-VAVE.

## MAME uPD933 (BSD-3-Clause)

The CZ engine follows `src/devices/sound/upd933.cpp` of MAME. The CZ's panel-to-chip conversions (envelope
rates and levels, key follow, vibrato tables) come from the CZ's published MIDI tone format.

Copyright (c) Devin Acker

Redistribution and use in source and binary forms, with or without modification, are permitted provided that
the following conditions are met:

1. Redistributions of source code must retain the above copyright notice, this list of conditions and the
   following disclaimer.
2. Redistributions in binary form must reproduce the above copyright notice, this list of conditions and the
   following disclaimer in the documentation and/or other materials provided with the distribution.
3. Neither the name of the copyright holder nor the names of its contributors may be used to endorse or
   promote products derived from this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED
WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE
GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

Casio and CZ are trademarks of Casio Computer Co., Ltd.; Ravel is not affiliated with Casio. The CZ presets in
Ravel are original patches, not Casio's.
