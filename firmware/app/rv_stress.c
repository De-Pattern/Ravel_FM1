/* SPDX-License-Identifier: GPL-3.0-only */
/* Ravel: the stress test (GLO > CPU). A project built to load the audio a step at a time, each step
 * adding to the last: voices of one engine, then strikes, two drum kits, then each effect
 * (the delay, its grains and its tape wear, the space, the feedback and its processor, the BED).
 * Held notes and a busy pattern keep the load steady, so the CPU page's reading at each step is what
 * that step costs on this machine. The overload guard stays off while it runs, so the step where the
 * XRUNS count starts is the real edge. It replaces the project in memory, as a demo does. */

static const char *const stress_names[RV_STRESS_N] = {
    "OFF", "IDLE", "1 VOICE", "2 VOICES", "4 VOICES", "8 VOICES", "+2 STRIKES", "+DRUM KIT",
    "+2ND KIT", "+DELAY", "+GRAINS", "+TAPE WEAR", "+SPACE", "+FEEDBACK", "+FB PROCESS", "+BED",
    "+MASTER DRIVE"};

const char *rv_stress_name(int step)
{
    return stress_names[clampi(step, 0, RV_STRESS_N - 1)];
}

enum { ST_DELAY = 9, ST_GRAINS, ST_WEAR, ST_SPACE, ST_FB, ST_FBPROC, ST_BED, ST_DRIVE };

static void stress_hit(int t, int s, int note)
{
    rv_step *p = &rv.pat[t][0].s[s];
    p->flags = ST_ON;
    p->note = (uint8_t)note;
    p->vel = 100;
    p->len = 2;
    p->chance = 100;
}

/* step 0 stops it all (the project stays as it is); any other builds the project for that step */
void rv_stress(int step, int engine)
{
    static const uint8_t chord[4] = {48, 55, 60, 64};
    int t, s, k;
    rv_all_off();
    rv_transport(0);
    if (step <= 0) {
        rv_live.guard_off = 0;
        return;
    }
    step = clampi(step, 1, RV_STRESS_N - 1);
    engine = clampi(engine, 0, ENG_N - 1);
    rv_live.guard_off = 1;                            /* the real edge, not the guard's */
    rv_project_init();
    put_str(rv.name, 0, 12, "STRESS");
    rv.gp[GP_TEMPO] = 120;
    rv.gp[GP_COUNTIN] = 0;
    for (t = 0; t < RV_TRACKS; t++)                   /* quiet, so all of it together stays clean */
        rv.tp[t][TP_LEVEL] = 50;
    /* T1, T2: the engine's voices, held */
    for (t = 0; t < 2; t++) {
        rv_track_defaults(t, KIND_SYNTH, engine);
        rv.tp[t][TP_POLY] = 3;
        rv.tp[t][TP_SUS] = 127;
        rv.tp[t][TP_LEVEL] = 40;
    }
    /* T3: a drum kit, a hit on every step, the lanes in turn */
    if (step >= 7) {
        rv_track_defaults(2, KIND_PERC, 1);
        rv.tp[2][TP_LEVEL] = 50;
        for (s = 0; s < 16; s++) {
            stress_hit(2, s, 0);
            rv.pat[2][0].s[s].note = (uint8_t)(1u << (s & 3));
        }
    }
    /* T5: two strike voices, hit on every other step */
    if (step >= 6) {
        rv_track_defaults(4, KIND_STRIKE, 0);
        rv.tp[4][TP_POLY] = 1;
        rv.tp[4][TP_LEVEL] = 50;
        for (s = 0; s < 16; s += 2)
            stress_hit(4, s, s & 2 ? 67 : 60);
    }
    /* T7: a second drum kit, the other lanes, a hit on every step */
    if (step >= 8) {
        rv_track_defaults(6, KIND_PERC, 1);
        rv.tp[6][TP_LEVEL] = 50;
        for (s = 0; s < 16; s++) {
            stress_hit(6, s, 0);
            rv.pat[6][0].s[s].note = (uint8_t)(1u << (4 + (s & 1)));
        }
    }
    /* the effects, one at a time: every track sends into each bus */
    for (t = 0; t < RV_TRACKS; t++) {
        if (step >= ST_DELAY)
            rv.tp[t][TP_DLYS] = 50;
        if (step >= ST_SPACE)
            rv.tp[t][TP_REVS] = 50;
        if (step >= ST_FB)
            rv.tp[t][TP_FBS] = 40;
    }
    if (step >= ST_GRAINS) {                         /* half the delay grains, busy */
        rv.gp[GP_DLYMODE] = 64;
        rv.gp[GP_GRDENS] = 100;
    }
    if (step >= ST_WEAR) {                           /* the tape worn: wow, flutter, age */
        rv.gp[GP_DLYWOB] = 60;
        rv.gp[GP_DLYFLUT] = 60;
        rv.gp[GP_DLYAGE] = 60;
    }
    if (step >= ST_FBPROC) {                         /* the loop's processor (the frequency shift) and its band */
        rv.gp[GP_FBPROC] = 3;
        rv.gp[GP_FBTUNE] = 90;
        rv.gp[GP_PAIRF1] = 20;
        rv.gp[GP_PAIRF2] = 110;
    }
    if (step >= ST_BED) {
        rv.gp[GP_BEDLEVEL] = 60;
        rv.gp[GP_BEDSHIM] = 40;
        rv.gp[GP_BEDSPACE] = 40;
    }
    rv.gp[GP_MDRIVE] = (int16_t)(step >= ST_DRIVE ? 60 : 0);
    rv_transport(1);
    /* the held notes: 1, 2, 4, then 8 voices of the engine */
    k = step >= 5 ? 8 : step >= 4 ? 4 : step >= 3 ? 2 : step >= 2 ? 1 : 0;
    for (s = 0; s < k; s++)
        rv_note(s >> 2, chord[s & 3] + (s >> 2) * 12, 100);
    if (step >= 8)
        for (s = 0; s < 3; s++)
            rv_note(6, chord[s + 1], 100);
}
