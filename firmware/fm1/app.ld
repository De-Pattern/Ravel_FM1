/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* Felucca memory map (Ravel: the pool takes the instrument's state).
 * Code + constants execute in place from XIP (app.bin offset 0 = 0x02000120).
 * 0x01C00000..0x01C08000 holds .ram_text (code that must run while the flash
 * is busy: the flash driver), core 1's loop and Ravel's .fast_text (the render's
 * busiest code). (Felucca kept 0x01C06000.. for RAM-run debug; Ravel uses it.)
 * RAM  : .data/.bss (96 KiB)
 * POOL : big zero-initialised buffers, section .pool. Ravel: all of the instrument's and the Beads
 *        port's zero-initialised state goes here (rv_app.o, bd.o), the platform's stays in RAM.
 * STACKS with 256-byte guard bands; NOINIT survives resets and UBOOT entry.
 * 0x01C7FD50.. is reserved: boot_info, boot-state mailbox, vector table. */
MEMORY
{
    XIP    (rx)  : ORIGIN = 0x02000120, LENGTH = 0x8DFBC
    RAMTEXT (rwx): ORIGIN = 0x01C00000, LENGTH = 0x8000
    RAM    (rwx) : ORIGIN = 0x01C08000, LENGTH = 96K
    POOL   (rw)  : ORIGIN = 0x01C20000, LENGTH = 0x54000
    NOINIT (rw)  : ORIGIN = 0x01C7C000, LENGTH = 0x3D50
}
SECTIONS
{
    .text : { KEEP(*(.text.crt0)) *(.text*) *(.rodata*) . = ALIGN(4); _etext = .; } > XIP
    .ram_text : { _rt_start = .; KEEP(*(.ram_text*)) . = ALIGN(4); } > RAMTEXT AT > XIP
    _rt_load = LOADADDR(.ram_text);
    .c1_text : { _c1_start = .; KEEP(*(.c1_text*)) . = ALIGN(4); _c1_end = .; } > RAMTEXT AT > XIP
    _c1_load = LOADADDR(.c1_text);
    /* the render's busiest code, run from RAM (RV_HOT in firmware/app) */
    .fast_text : { _fast_start = .; KEEP(*(.fast_text*)) . = ALIGN(4); _fast_end = .; } > RAMTEXT AT > XIP
    _fast_load = LOADADDR(.fast_text);
    _rt_end = _fast_end;                /* the guard's RAM code window covers all three */
    .data : { . = ALIGN(4); _data_start = .; *(.data*) *(.sdata*) . = ALIGN(4); _data_end = .; } > RAM AT > XIP
    _data_load = LOADADDR(.data);
    .bss (NOLOAD) : { _bss_start = .; *(.rv_ram_bss*) *(EXCLUDE_FILE(*rv_app.o *bd.o) .bss*) *(EXCLUDE_FILE(*rv_app.o *bd.o) .sbss*)
                     *(EXCLUDE_FILE(*rv_app.o *bd.o) COMMON) . = ALIGN(4); _bss_end = .; } > RAM
    /* more render code (RV_HOT2: the voices' engines, the drum machines), at the pool's start: the
     * tape's 120 KB, gone, made room. Copied at start like .fast_text; the guard's RAM code window
     * reaches to its end (_xram_end) */
    .pool_text : { _pt_start = .; KEEP(*(.pool_text*)) . = ALIGN(4); _pt_end = .; } > POOL AT > XIP
    _pt_load = LOADADDR(.pool_text);
    _xram_end = _pt_end > _pt_start ? _pt_end : _rt_end;
    .pool (NOLOAD) : { _pool_start = .; *(.pool*) *rv_app.o(.bss* .sbss* COMMON) *bd.o(.bss* .sbss* COMMON)
                       . = ALIGN(4); _pool_end = .; } > POOL
    .noinit (NOLOAD) : { *(.noinit*) } > NOINIT
    _guard0 = 0x01C74000; _ustack_lo = 0x01C74100; _ustack_top = 0x01C7A000;
    _guard1 = 0x01C7A000; _sstack_lo = 0x01C7A100; _sstack_top = 0x01C7C000;
    /DISCARD/ : { *(.comment) *(.note*) }
}
