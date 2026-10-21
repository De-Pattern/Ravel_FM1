/* SPDX-License-Identifier: GPL-3.0-only
 * Boot, audio ISR and main loop adapted from X0X's main_fm1.c, itself from Felucca's main.c /
 * audio.c, Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hugelton Instruments. */
/* Ravel on the FM-1: boot (WDT first, boot-loop guard, guards), LCD, input (TIMER5 10 kHz), audio
 * (ALNK0), USB; then the main loop: the interface about 30 frames a second, input polled between. */
extern uint32_t _data_start[], _data_end[], _data_load[], _bss_start[], _bss_end[];
extern uint32_t _pool_start[], _pool_end[], _rt_start[], _rt_end[], _rt_load[], _c1_start[], _c1_end[], _c1_load[];

/* --------------------------------------------------------------- audio --- */
#define HALF_FRAMES 512u                    /* I2S half buffer: 11.6 ms at 44.1 kHz */
#define HALF_WORDS (HALF_FRAMES * 2u)
#define REND_FRAMES 256u                    /* one rv_render call (usb.c's UA_HALF) */
#if FELUCCA_UAC
_Static_assert(REND_FRAMES == UA_HALF, "usb.c sizes the USB audio ring band for this block");
#endif
static int32_t abuf[2u * HALF_WORDS] __attribute__((aligned(4)));
static volatile uint8_t in_audio;
static uint8_t safe_mode;

/* rv_render gives full scale as 2^23; the codec's comfortable ceiling is Felucca's 2^22 (-6 dBFS).
 * USB gets that level; the speaker, headphones and line out get the MASTER knob after it, ramped */
static uint32_t gain_q12;
static void render_block(int32_t *o)
{
    uint32_t t0 = fm1_ticks(), us, budget = HALF_FRAMES * 1000000u / 44100u, k, i;
    in_audio = 1;
#if FELUCCA_UAC
    uac_render_start();
#endif
    for (k = 0; k < HALF_FRAMES; k += REND_FRAMES) {
        int32_t *b = o + 2u * k;
        rv_render(b, (int)REND_FRAMES);
        for (i = 0; i < 2u * REND_FRAMES; i++)
            b[i] >>= 1;
#if FELUCCA_UAC
        uac_tap(b, REND_FRAMES);
#endif
        for (i = 0; i < 2u * REND_FRAMES; i++) {
            if ((i & 15u) == 0u) {
                uint32_t target = master_q12;
                if (gain_q12 < target)
                    gain_q12 += target - gain_q12 > 16u ? 16u : target - gain_q12;
                else if (gain_q12 > target)
                    gain_q12 -= gain_q12 - target > 16u ? 16u : gain_q12 - target;
            }
            b[i] = (int32_t)(((int64_t)b[i] * (int32_t)gain_q12) >> 12);
        }
    }
    us = (fm1_ticks() - t0) / FM1_TICKS_PER_US;
    audio_cpu_pct = (audio_cpu_pct * 15u + us * 100u / budget) / 16u;
    in_audio = 0;
}

void fm1_alnk0_irq(void)                            /* via isr_alnk0 (hal/fm1_isr.S) */
{
    uint8_t p = fm1_audio_pending();
    fm1_audio_ack_aux(p);
    if (p & FM1_AUDIO_HALF) {
        uint32_t half = fm1_audio_free_half();
        render_block(&abuf[half * HALF_WORDS]);
        fm1_audio_ack_half();
        if (fm1_audio_free_half() != half)
            audio_xruns++;                          /* the DMA moved on while we rendered */
    }
}
extern void isr_alnk0(void);

static void audio_init(void)
{
    uint32_t i;
    for (i = 0; i < 2u * HALF_WORDS; i++)
        abuf[i] = 0;
    fm1_audio_init(abuf, HALF_WORDS, isr_alnk0, 3);
}

static void audio_silence(void)                     /* IRQs off (flash erase): the DMA would loop stale audio */
{
    uint32_t i;
    for (i = 0; i < sizeof abuf / sizeof abuf[0]; i++)
        abuf[i] = 0;
}

/* ------------------------------------------------------------ timer --- */
static uint8_t usb_due;
void fm1_timer5_irq(void)
{
    static uint32_t sub;
    fm1_timer5_ack();
    fm1_input_tick();
    /* 2 kHz: USB. The SIE traffic waits for a tick outside the render (it shares the MIDI queues);
     * the USB audio stream cannot wait, so it runs nested (X0X) */
    if (sub % 5u == 0u) {
        usb_due = 1;
#if FELUCCA_UAC
        if (in_audio)
            uac_service();
#endif
    }
    if (usb_due && !in_audio) {
        usb_due = 0;
        usb_poll();
#if RAVEL_TRS
        uart_midi_poll();
#endif
    }
    if (++sub == 10u)
        sub = 0;
    {
        static uint32_t last, acc;
        uint32_t now = fm1_ticks();
        acc += now - last;
        last = now;
        while (acc >= 1000u * FM1_TICKS_PER_US) {
            acc -= 1000u * FM1_TICKS_PER_US;
            fm1_ms++;
        }
    }
}
extern void isr_timer5(void);

/* ------------------------------------------------------------- boot --- */
#define BOOTGUARD_MAGIC 0x42475244u
struct bootguard_s bootguard __attribute__((section(".noinit")));

static void hexs(char *b, uint32_t v)
{
    uint32_t i;
    for (i = 0; i < 8u; i++)
        b[i] = "0123456789ABCDEF"[(v >> (28u - 4u * i)) & 15u];
    b[8] = 0;
}

static void fm1_fault(const fm1_crash_t *c)
{
    char pc[12] = "PC ", vec[16] = "VEC ";
    uint32_t t0;
    fm1_audio_stop();
    hexs(pc + 3, c->pc);
    hexs(vec + 4, c->vec);
    ui_notice("RAVEL CRASHED", pc, vec, "RESTARTING", 1);
    t0 = fm1_ticks();
    while ((uint32_t)(fm1_ticks() - t0) < 4000u * 1000u * FM1_TICKS_PER_US)
        ;
    fm1_reboot();
}

static void enter_uboot(const char *why)
{
    fm1_audio_stop();
    ui_notice(why, "UPDATE MODE", 0, 0, 0);
    fm1_delay_ms(20);
    usb_detach();
    fm1_delay_ms(30);
    bootguard.pending = 0;                          /* intentional reset: not a failed boot */
    fm1_enter_uboot();
}

/* SAFE MODE: Ravel crashed (or hung) twice while starting. Nothing that makes sound runs; USB is on,
 * so the web installer (Ravel's, the stock firmware's) and M-UPGRADE can reach it. PLAY tries Ravel
 * again. The chip's own update mode stays the last resort (fm1_cstart). */
static void safe_main(void)
{
    uint32_t t0, play, both, t_oct;
    ui_notice("SAFE MODE", "RAVEL CRASHED TWICE STARTING", "USB IS ON: REINSTALL FROM THE WEB",
              "PLAY: TRY AGAIN", 0);
    fm1_input_init();
    panel_init();
    usb_start();
    fm1_timer5_start(isr_timer5, 1);
    fm1_guard_lock_top();
    fm1_irq_enable_all();
    play = 1u << panel.btn[B_PLAY];
    both = (1u << panel.btn[B_OCTDN]) | (1u << panel.btn[B_OCTUP]);
    t0 = t_oct = fm1_ms;
    for (;;) {
        fm1_wdt_feed();
        usb_retry(fm1_ms);
        if (bootguard.pending && fm1_ms - t0 > 10000u)
            bootguard.pending = 0;                  /* safe mode itself is up: not another failed boot */
        if ((fm1_in.buttons & play) && fm1_ms - t0 > 500u) {
            bootguard.failed = 0;
            bootguard.pending = 0;
            ui_notice("STARTING RAVEL", 0, 0, 0, 0);
            usb_detach();
            fm1_delay_ms(30);
            fm1_reboot();
        }
        if ((fm1_in.buttons & both) != both)        /* OCT- + OCT+ held 5 s: the chip's update mode */
            t_oct = fm1_ms;
        else if (fm1_ms - t_oct > 5000u)
            enter_uboot("UBOOT");
        ota_service();
        if (usb.ota_req) {
            usb.ota_req = 0;
            if (flash_ok)
                ota_session();                      /* an install commits and resets; else back here */
        }
        if (usb.uboot_req)
            enter_uboot("UBOOT (USB)");
    }
}

static void fm1_main(void)
{
    int32_t knob = 512 * 16;
    uint32_t last_frame = 0;
    {
        uint32_t f = irq_save();
        fm1_vdc14_raise(7);                         /* M-VAVE's own supplies before anything loads them */
        fm1_sysvdd_raise(15);
        flash_ok = FL_FAR(fl_jedec_ram)() == 0x856014u;   /* the expected 1 MiB part, else RAM only */
        irq_restore(f);
        if (flash_ok)
            ota_boot_cleanup();
    }
    lcd_init();
    ui_notice("RAVEL", "SYNTHESIZE  CATCH  REBUILD", 0, 0, 0);
    if (safe_mode)
        safe_main();                                /* never returns */
    if (fm1_crash.magic == FM1_CRASH_MAGIC) {     /* a crash since power-on: say so, once */
        char b[12] = "PC ";
        hexs(b + 3, fm1_crash.pc);
        ui_notice("RAVEL", "RESTARTED AFTER A CRASH", b, 0, 0);
        fm1_wdt_feed();
        fm1_delay_ms(1500);
        fm1_wdt_feed();
        fm1_crash.magic = 0;                        /* said once */
    }
    fm1_input_init();
    fm1_adc_init();
    panel_init();
    led_pos_init();
    ravel_start();                                  /* the last session (slot 1), else the demo */
    audio_init();
    usb_start();
#if RAVEL_TRS
    uart_midi_init();                               /* before TIMER5: it read-modify-writes port H too */
#endif
    /* above ALNK0 (3): the key / encoder / LED matrix is scanned one column per tick and must keep
     * its rhythm through a render (X0X) */
    fm1_timer5_start(isr_timer5, 4);
    fm1_guard_lock_top();
    fm1_irq_enable_all();
    fm1_delay_ms(30);
    if ((fm1_in.buttons & ((1u << panel.btn[B_OCTDN]) | (1u << panel.btn[B_OCTUP]))) ==
        ((1u << panel.btn[B_OCTDN]) | (1u << panel.btn[B_OCTUP])))
        panel_setup();                              /* OCT- + OCT+ held at power-on: calibration */
    else if ((fm1_in.buttons & ((1u << panel.btn[B_HOME]) | (1u << panel.btn[B_SAVE]))) ==
             ((1u << panel.btn[B_HOME]) | (1u << panel.btn[B_SAVE])))
        moom_start();                               /* HOME + SAVE held at power-on: the hidden game */

    for (;;) {
        fm1_wdt_feed();
        usb_retry(fm1_ms);
        if (fm1_ms > 30000u && bootguard.pending) { /* a crash or hang in the first 30 s counts */
            bootguard.pending = 0;
            bootguard.failed = 0;
        }
        {
            int32_t a = fm1_adc_read(FM1_ADC_MASTER);
            if (a >= 0) {
                uint32_t k10;
                knob += (a * 16 - knob) / 8;
                k10 = (uint32_t)(knob / 16);
                master_q12 = (k10 * k10) >> 8;      /* 0 .. ~4096, an audio taper */
            }
        }
        {   /* OCT- + OCT+ held 5 s: update mode (Felucca's) */
            static uint32_t t0;
            uint32_t both = (1u << panel.btn[B_OCTDN]) | (1u << panel.btn[B_OCTUP]);
            if ((fm1_in.buttons & both) != both)
                t0 = fm1_ms;
            else if (fm1_ms - t0 > 5000u)
                enter_uboot("UBOOT");
        }
        {   /* M-UPGRADE handshake; Ravel's own messages: projects and sounds to and from a computer */
            uint32_t d = ota_service();
            if (d >= 7u && ota_dec[2] >= 0x60u && ota_dec[2] <= 0x64u)
                rv_xfer_msg(ota_dec, d);
        }
        if (usb.ota_req) {
            usb.ota_req = 0;
            rv_transport(0);
            if (flash_ok)
                ota_session();                      /* returns only if nothing was committed */
        }
        if (usb.uboot_req)
            enter_uboot("UBOOT (USB)");
#if RAVEL_CDC
        cdc_task();
#endif
        if (fm1_ms - last_frame >= 33u) {           /* ~30 frames/s; input in between */
            last_frame = fm1_ms;
            ui_frame();
        } else {
            ui_input_only();
        }
    }
}

void fm1_cstart(void)
{
    uint32_t *s, *d, p3, src, wdt;
    fm1_cpu1_halt();                                /* a second core left running by the reset would read
                                                     * the RAM cleared below (fm1-x0x#10); Ravel runs on one */
    fm1_time_init();
    fm1_reset_reason();
    p3 = fm1_boot.p3_rst;
    src = fm1_boot.rst_src;
    wdt = fm1_boot.wdt_con;
    fm1_wdt_arm(0x0D);
    if (bootguard.magic != BOOTGUARD_MAGIC) {
        bootguard.magic = BOOTGUARD_MAGIC;
        bootguard.failed = 0;
        bootguard.pending = 0;
    }
    if (bootguard.pending)
        bootguard.failed++;
    bootguard.pending = 1;
    if (bootguard.failed >= 4u) {                   /* safe mode failed too: the chip's own update mode */
        bootguard.failed = 0;
        bootguard.pending = 0;
        fm1_enter_uboot();
    }
    p3 |= (bootguard.failed >= 2u) << 8;            /* two failed boots: safe mode (kept past the .bss clear) */
    fm1_irq_init();
    irq_stack_mark();
    for (d = _bss_start; d < _bss_end; d++)
        *d = 0;
    for (d = _pool_start; d < _pool_end; d++)
        *d = 0;
    for (s = _data_load, d = _data_start; d < _data_end; s++, d++)
        *d = *s;
    for (s = _rt_load, d = _rt_start; d < _rt_end; s++, d++)
        *d = *s;
    for (s = _c1_load, d = _c1_start; d < _c1_end; s++, d++)
        *d = *s;
    fm1_mailbox_clear();
    fm1_guard_enable(FM1_GUARD_STACK | FM1_GUARD_WRITE | FM1_GUARD_BUS | FM1_GUARD_PC);
    safe_mode = (uint8_t)(p3 >> 8);
    fm1_boot.p3_rst = (uint8_t)p3;
    fm1_boot.rst_src = src;
    fm1_boot.wdt_con = (uint8_t)wdt;
    fm1_main();
    for (;;)
        ;
}
