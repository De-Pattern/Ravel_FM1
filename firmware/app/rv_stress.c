/* SPDX-License-Identifier: GPL-3.0-only */
/* Ravel: the stress test (GLO > CPU). A project built to load the audio a step at a time, each step
 * adding to the last: voices of one engine, then strikes, a drum kit, sample voices, then each bus.
 * Held notes and a busy pattern keep the load steady, so the CPU page's reading at each step is what
 * that step costs on this machine. The overload guard stays off while it runs, so the step where the
 * XRUNS count starts is the real edge. It replaces the project in memory, as a demo does. */

static const char *const stress_names[RV_STRESS_N] = {
    "OFF", "IDLE", "1 VOICE", "2 VOICES", "4 VOICES", "8 VOICES", "+2 STRIKES", "+DRUM KIT",
    "+3 SAMPLES", "+DELAY", "+SPACE ROOM", "SPACE CLOUDS", "+FEEDBACK", "+PAIR", "+BANDS", "+VOCODER",
    "+MASTER DRIVE"};

const char *rv_stress_name(int step)
{
    return stress_names[clampi(step, 0, RV_STRESS_N - 1)];
}

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
    /* T7: three sample voices across the keys, held */
    if (step >= 8) {
        rv_track_defaults(6, KIND_TAPE, 0);
        rv.tp[6][TP_S0] = TAPE_KEYS;
        rv_track_src_defaults(6);
        rv.tp[6][TP_SUS] = 127;
    }
    /* the buses, one at a time: every track sends into each */
    for (t = 0; t < RV_TRACKS; t++) {
        if (step >= 9)
            rv.tp[t][TP_DLYS] = 50;
        if (step >= 10)
            rv.tp[t][TP_REVS] = 50;
        if (step >= 12)
            rv.tp[t][TP_FBS] = 40;
        if (step >= 13)
            rv.tp[t][TP_PAIRS] = 40;
        if (step >= 14)
            rv.tp[t][TP_BANDS] = 40;
        if (step >= 15)
            rv.tp[t][TP_VOC] = 40;
    }
    rv.gp[GP_REVMODE] = (int16_t)(step >= 11 ? 1 : 0);
    rv.gp[GP_MDRIVE] = (int16_t)(step >= 16 ? 60 : 0);
    rv_transport(1);
    /* the held notes: 1, 2, 4, then 8 voices of the engine */
    k = step >= 5 ? 8 : step >= 4 ? 4 : step >= 3 ? 2 : step >= 2 ? 1 : 0;
    for (s = 0; s < k; s++)
        rv_note(s >> 2, chord[s & 3] + (s >> 2) * 12, 100);
    if (step >= 8)
        for (s = 0; s < 3; s++)
            rv_note(6, chord[s + 1], 100);
}
