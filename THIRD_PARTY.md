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
| Barlow Semi Condensed (screen fonts) | `assets/fonts/` | SIL Open Font License 1.1 |
| PNG writer of the desktop simulator | `host/ravel_host.c` | GPL-3.0 (from X0X) |

Not in this repository, fetched at build time: JieLi's Linux toolchain (`tools/get_toolchain.sh`) and three files
of JieLi's AC79 SDK, `AC79NN_SDK_V1.2.1_2023-12-13` (Apache-2.0): `cpu/wl82/tools/uboot.boot`, `cfg_tool.bin` and
`cfg/eq_cfg_hw.bin`. A firmware package contains those three files, as Felucca's and X0X's packages do.

Ravel uses no M-VAVE code. M-VAVE and FM-1 are trademarks of their owners; Ravel is not affiliated with M-VAVE.
