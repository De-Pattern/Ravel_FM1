# M-VAVE FM-1 V15 recovery image

`FM-1_015.fwsc` is M-VAVE's stock FM-1 firmware V15 (identity `FM-1_015`, SHA-256
`db1642b2b6fa5c2cccb11ffd13878068bb28601678d3644049f99dc40e7edb8a`), unmodified, as bundled in M-VAVE's FM1 Open SDK
(https://github.com/m-vave/fm1_open, commit `4c7496daac04a95ff10d1afdcc8f9ead2dbabc41`, `sdk/firmware/FM-1_015.fwsc`).

M-VAVE distributes it under the Apache License, Version 2.0 (`LICENSE-Apache-2.0.txt`), per the SDK's
`THIRD_PARTY.md`, "Boot/update resources and recovery firmware":

> The boot/update files in `sdk/tools/pack/` (`uboot.boot`, `ota.bin`, `cfg_tool.bin`, `boot-config.bin`) and the
> recovery image `sdk/firmware/FM-1_015.fwsc` are distributed by M-VAVE under the Apache License, Version 2.0.

Copyright (c) 2026 珠海笙科智能科技有限公司 (M-VAVE). Ravel is not affiliated with or endorsed by M-VAVE.

Ravel's install page uses it for "Back to the stock firmware": the file itself is not changed. The page sends
its `flash.bin` with Ravel's own update loader (`ota-stock.bin`) in place of M-VAVE's when the FM-1 runs Ravel
(see firmware/fm1/loader/ldr_core.c, LDR_TO_STOCK), and the file as it is otherwise.
