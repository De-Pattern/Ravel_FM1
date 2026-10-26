/* SPDX-License-Identifier: GPL-3.0-only */
/* Ravel PERC tracks: drum machines after the Elektron Machinedrum's synthesis machines (TRX, EFM,
 * P-I and GND). Elektron's code was never released; these are Ravel's own designs, built from what
 * the Machinedrum's manual documents: each machine's parameters, in its order, doing what the
 * manual says they do, and the track effects that follow every machine (amplitude modulation, a
 * one-band EQ, the high-pass/low-pass band filter). SRR and DIST are the track's own DEREZ and
 * DRIVE (SKIN). The machine is SOURCE knob 1, its parameters the next eight, the track effects the
 * last seven.
 *
 * TRX: analog-style drums in the TR tradition (sines with pitch ramps, noise, square-wave metal).
 * EFM: two-operator FM drums with feedback.
 * P-I: physically informed: modal resonators (membranes, bars, cymbals) struck by a mallet model.
 * GND: a sine, noise, and an impulse. */

/* ---------------------------------------------------------------- helpers --- */
RV_INL float pk_dec(float x, float lo, float hi) { return lo * fm_exp2f(x * fm_log2f(hi / lo)); }   /* x 0..1 */
RV_INL float pk_coef(float sec) { return fm_expf(-RV_DT / (sec > 0.0005f ? sec : 0.0005f)); }
RV_INL float pk_sin(float ph) { return fm_sin_turns(ph - fm_floorf(ph)); }
RV_INL float pk_noise(perc_t *v) { return xs_bi(&v->rng); }

/* a modal resonator: hz, decay in seconds, gain at the peak */
RV_INL void pk_mode(perc_t *v, int k, float hz, float dec, float g)
{
    float w, r;
    hz = clampf(hz, 20.0f, RV_NYQ * 0.92f);
    w = 6.2831853f * hz * RV_DT;
    r = fm_expf(-RV_DT / (dec > 0.001f ? dec : 0.001f));
    v->mb1[k] = 2.0f * r * fm_cosf(w);
    v->mb2[k] = -r * r;
    v->mg[k] = g * fm_sinf(w);
}
RV_INL float pk_modes(perc_t *v, int n, float x)
{
    float s = 0.0f;
    int k;
    for (k = 0; k < n; k++) {
        float y = v->mb1[k] * v->my1[k] + v->mb2[k] * v->my2[k] + v->mg[k] * x;
        v->my2[k] = v->my1[k];
        v->my1[k] = y;
        s += y;
    }
    return s;
}

/* the TRX hat and cymbal metal: six square waves at inharmonic ratios */
static const float pk_metal[6] = {1.0f, 1.483f, 1.801f, 2.546f, 2.630f, 3.897f};

/* the next kit hit's locks, found by the sequencer just before it plays (live hits have none) */
static uint16_t perc_pend_mask;
static uint8_t perc_pend_val[9];
static void perc_lock_prepare(int t, int lane, int pat, int step)
{
    const rv_kitlock *k = rv.kitlk[t & 7];
    uint8_t st = (uint8_t)((step & 63) | ((pat & 1) << 6));
    int i;
    perc_pend_mask = 0;
    for (i = 0; i < RV_KITLOCKS; i++)
        if (k[i].what != 0xFF && k[i].step == st && (k[i].what >> 4) == lane) {
            int f = k[i].what & 15;
            if (f <= KL_LEVEL) {
                perc_pend_mask |= (uint16_t)(1u << f);
                perc_pend_val[f] = k[i].val;
            }
        }
}

/* a KIT's lane from a key: C4 and up are lanes 1-6, other octaves fold onto them */
static int perc_lane_of(int note)
{
    int k = ((note - 60) % 12 + 12) % 12;
    return k < RV_LANES ? k : k % RV_LANES;
}

/* mach: the machine (-1: the track's SOURCE 1); lane: the kit lane, -1 for a single machine */
static void perc_note_on(perc_t *v, int note, float vel, const float *eff, uint32_t rng, int mach, int lane)
{
    int k;
    if (!v->on) {                                     /* a fresh voice: clear it, keeping whose it is */
        uint8_t trk_ = v->track;
        uint32_t age_ = v->age;
        __builtin_memset(v, 0, sizeof(*v));
        v->track = trk_;
        v->age = age_;
    } else {                                          /* retrigger: keep the filters, restart the drum */
        for (k = 0; k < 6; k++)
            v->ph[k] = 0.0f;
    }
    v->on = 1;
    v->gate = 1;
    v->note = (int8_t)note;
    v->vel = vel;
    v->rng = rng;
    v->t = 0.0f;
    v->mach = (uint8_t)clampi(mach >= 0 ? mach : (int)(eff[TP_S0] + 0.5f), 0, PM_N - 1);
    v->lane = (int8_t)lane;
    v->lk_mask = 0;
    if (lane >= 0) {
        v->note = 60;                                 /* a lane plays its machine at its own pitch */
        v->lk_mask = perc_pend_mask;                  /* and with this step's locks */
        for (k = 0; k < 9; k++)
            v->lk_val[k] = perc_pend_val[k];
    }
    perc_pend_mask = 0;
    v->aenv = 1.0f;
    v->penv = 1.0f;
    v->menv = 1.0f;
    v->nenv = 1.0f;
    v->xenv = 1.0f;
    v->cnt = 0;
    v->next = 0.0f;
    v->exc = 1.0f;
    v->quiet = 0;
    for (k = 0; k < 8; k++)
        v->my1[k] = v->my2[k] = 0.0f;
}

/* one block of the machine, mono, into out (overwrites); p: the eight parameters 0..1 */
RV_HOT2 static void perc_machine(perc_t *v, const float *p, float semi, float *out, int n)
{
    int i, k;
    float tr = fm_exp2f(semi * (1.0f / 12.0f));       /* the key transposes every pitched machine */
    switch (v->mach) {
    case PM_TRX_BD: case PM_TRX_B2: {
        int b2 = v->mach == PM_TRX_B2;
        float f0 = 28.0f * fm_exp2f(p[0] * 2.4f) * tr, ca = pk_coef(pk_dec(p[1], 0.04f, 2.5f));
        float ramp = p[2] * 5.0f, cp = pk_coef(b2 ? 0.035f : pk_dec(p[3], 0.004f, 0.2f));
        float hold = b2 ? p[3] * 0.25f : 0.0f, strt = p[4], nois = p[5] * 0.6f;
        float harm = b2 ? 0.0f : p[6], drv = 1.0f + p[7] * 9.0f, dirt = b2 ? p[6] : 0.0f;
        for (i = 0; i < n; i++) {
            float f = f0 * (1.0f + ramp * v->penv), y;
            v->ph[0] += f * RV_DT;
            v->ph[0] -= fm_floorf(v->ph[0]);
            y = pk_sin(v->ph[0]);
            if (harm > 0.0f)                           /* HARM: toward a rounded triangle, with its overtones */
                y = y + harm * (pk_sin(v->ph[0] * 2.0f) * 0.4f + pk_sin(v->ph[0] * 3.0f) * 0.25f);
            if (v->t < 0.004f)                         /* STRT / TICK: a hard click at the start */
                y += strt * (1.0f - v->t * 250.0f) * (b2 ? pk_noise(v) * 0.6f + 0.8f : 1.2f);
            y += pk_noise(v) * nois * v->nenv;
            y *= v->aenv;
            if (dirt > 0.0f) {                         /* DIRT: fewer bits */
                float q = fm_exp2f(12.0f - dirt * 9.0f);
                y = fm_floorf(y * q + 0.5f) / q;
            }
            out[i] = b2 ? sat(y * drv + 0.15f * y * y * drv) / sat(drv) : sat(y * drv) / sat(drv);
            v->penv *= cp;
            v->nenv *= 0.9985f;
            if (v->t >= hold)
                v->aenv *= ca;
            v->t += RV_DT;
        }
        break;
    }
    case PM_TRX_SD: {
        float f0 = 110.0f * fm_exp2f(p[0] * 2.0f) * tr, ca = pk_coef(pk_dec(p[1], 0.04f, 0.8f));
        float bump = p[2] * 1.5f, cb = pk_coef(pk_dec(p[3], 0.003f, 0.08f)), snap = p[4];
        float tone = 1500.0f * fm_exp2f(p[5] * 3.2f), tune = 1.3f + p[6] * 0.6f, drv = 1.0f + p[7] * 8.0f;
        float cn = pk_coef(pk_dec(p[1], 0.06f, 0.6f));
        svf_set(&v->f1, tone, 0.9f);
        for (i = 0; i < n; i++) {
            float f = f0 * (1.0f + bump * v->penv), y, ns, bp;
            v->ph[0] += f * RV_DT;
            v->ph[1] += f * tune * RV_DT;
            y = (pk_sin(v->ph[0]) + 0.55f * pk_sin(v->ph[1])) * v->aenv * 0.7f;
            ns = pk_noise(v);
            svf_tick(&v->f1, ns, &bp, 0);
            y += (bp * 0.7f + ns * 0.3f) * snap * v->nenv;
            out[i] = sat(y * drv) / sat(drv);
            v->penv *= cb;
            v->aenv *= ca;
            v->nenv *= cn;
            v->t += RV_DT;
        }
        break;
    }
    case PM_TRX_XT: case PM_TRX_XC: {
        int conga = v->mach == PM_TRX_XC;
        float f0 = (conga ? 140.0f : 55.0f) * fm_exp2f(p[0] * 2.5f) * tr, dec = pk_dec(p[1], 0.05f, conga ? 1.2f : 2.0f);
        float ca = pk_coef(dec), cd = pk_coef(dec * (1.0f - p[4] * 0.85f)), ramp = p[2] * 1.5f;
        float cp = pk_coef(pk_dec(p[3], 0.005f, 0.4f)), drv = 1.0f + p[5] * 10.0f, typ = p[6];
        for (i = 0; i < n; i++) {
            float f = f0 * (1.0f + ramp * v->penv), y;
            v->ph[0] += f * RV_DT;
            y = pk_sin(v->ph[0]);
            if (conga) {                               /* a skin's second mode and a slap */
                v->ph[1] += f * 1.59f * RV_DT;
                y += 0.3f * pk_sin(v->ph[1]) * v->xenv;
                if (v->t < 0.003f)
                    y += pk_noise(v) * 0.5f;
            }
            y *= v->aenv;
            if (drv > 1.01f) {                          /* DIST, DTYP from soft to folded */
                float s = sat(y * drv) / sat(drv), fo = fm_sinf(y * drv * 1.2f);
                y = s + (fo - s) * typ;
            }
            out[i] = y;
            v->penv *= cp;
            v->aenv *= v->aenv > 0.3f ? ca : cd;        /* DAMP: the tail dies faster */
            v->xenv *= 0.9992f;
            v->t += RV_DT;
        }
        break;
    }
    case PM_TRX_CP: {
        int claps = 2 + (int)(p[0] * 5.0f);
        float bpf = 700.0f * fm_exp2f(p[1] * 2.0f), hard = p[2], rich = p[3];
        float rate = pk_dec(p[4], 0.004f, 0.03f), room = p[5], rdec = pk_coef(pk_dec(p[6], 0.05f, 0.8f));
        float cc = pk_coef(0.012f - hard * 0.008f), cl = pk_coef(0.12f);
        svf_set(&v->f1, bpf, 3.0f - rich * 2.4f);
        svf_set(&v->f2, 500.0f * fm_exp2f(p[7] * 4.0f), 0.8f);
        for (i = 0; i < n; i++) {
            float ns = pk_noise(v), bp, bp2, y;
            if (v->cnt < claps && v->t >= v->next) {   /* the next clap */
                v->cnt++;
                v->next = v->t + rate * (0.8f + 0.4f * (pk_noise(v) * 0.5f + 0.5f));
                v->aenv = v->cnt == claps ? 1.0f : 0.8f;
            }
            svf_tick(&v->f1, ns, &bp, 0);
            y = bp * (v->cnt == claps ? v->aenv * (v->t - v->next + rate < 0.0f ? 1.0f : 1.0f) : v->aenv);
            svf_tick(&v->f2, ns, &bp2, 0);
            y += bp2 * room * v->nenv * 0.6f;           /* ROOM: a tail, RSIZ long, RTUN its colour */
            out[i] = y * 1.4f;
            v->aenv *= v->cnt == claps ? cl : cc;
            if (v->cnt >= 1)
                v->nenv *= rdec;
            v->t += RV_DT;
        }
        break;
    }
    case PM_TRX_RS: case PM_TRX_CL: {
        int cl = v->mach == PM_TRX_CL;
        float f0 = (cl ? 1300.0f : 420.0f) * fm_exp2f((p[0] - 0.5f) * 2.0f) * tr;
        float ca = pk_coef(pk_dec(p[1], 0.008f, cl ? 0.25f : 0.15f)), drv = cl ? 1.0f : 1.0f + p[2] * 10.0f;
        float dual = cl ? p[2] : 0.0f, enh = cl ? p[3] : 0.6f, r2 = cl ? 1.5f + p[4] * 2.0f : 2.27f, clic = cl ? p[5] : 0.6f;
        for (i = 0; i < n; i++) {
            float y;
            v->ph[0] += f0 * RV_DT;
            v->ph[1] += f0 * r2 * RV_DT;
            y = (pk_sin(v->ph[0]) + enh * pk_sin(v->ph[1])) * v->aenv;
            if (v->t < 0.0015f)
                y += clic * (pk_noise(v) * 0.5f + 0.7f);
            if (dual > 0.0f && v->t > 0.012f && v->t < 0.0125f)
                v->aenv = fm_maxf(v->aenv, dual);     /* DUAL: a second attack */
            out[i] = sat(y * drv) / sat(drv);
            v->aenv *= ca;
            v->t += RV_DT;
        }
        break;
    }
    case PM_TRX_CB: {
        float f0 = 560.0f * fm_exp2f((p[0] - 0.5f) * 2.0f) * tr, dec = pk_dec(p[1], 0.03f, 1.0f);
        float ca = pk_coef(dec * 0.25f), ct = pk_coef(dec * (1.0f - p[3] * 0.9f)), enh = p[2], bump = p[5] * 0.5f;
        svf_set(&v->f1, 900.0f * fm_exp2f(p[4] * 2.0f), 1.6f);
        for (i = 0; i < n; i++) {
            float f = f0 * (1.0f + bump * v->penv), sq, bp;
            v->ph[0] += f * RV_DT;
            v->ph[1] += f * 1.48f * RV_DT;
            v->ph[0] -= fm_floorf(v->ph[0]);
            v->ph[1] -= fm_floorf(v->ph[1]);
            sq = (v->ph[0] < 0.5f ? 1.0f : -1.0f) + (v->ph[1] < 0.5f ? 1.0f : -1.0f);
            svf_tick(&v->f1, sq, &bp, 0);
            out[i] = bp * (v->aenv * (1.0f - enh * 0.5f) + v->nenv * enh) * 0.6f;   /* ENH: the body's longer tail */
            v->aenv *= ca;
            v->nenv *= ct;
            v->penv *= 0.997f;
            v->t += RV_DT;
        }
        break;
    }
    case PM_TRX_CH: case PM_TRX_OH: case PM_TRX_CY: {
        int cy = v->mach == PM_TRX_CY, oh = v->mach == PM_TRX_OH;
        float base = cy ? 220.0f * fm_exp2f((p[4] - 0.5f) * -1.5f) : 330.0f, gap = cy ? 1.0f + p[0] * 0.3f : 0.7f + p[0] * 0.6f;
        float dec = cy ? pk_dec(p[1], 0.2f, 4.0f) : oh ? pk_dec(p[1], 0.06f, 1.5f) : pk_dec(p[1], 0.01f, 0.25f);
        float ca = pk_coef(dec), mtal = cy ? 0.6f + p[0] * 0.3f : p[4], top = cy ? p[2] : 0.4f;
        if (cy)
            base *= fm_exp2f((p[3] - 0.5f) * 1.0f);
        svf_set(&v->f1, cy ? 3000.0f : 2000.0f * fm_exp2f(p[2] * 3.0f), cy ? 0.7f + p[5] * 6.0f : 0.8f);
        svf_set(&v->f2, cy ? 9000.0f * fm_exp2f((p[3] - 0.5f)) : 3000.0f * fm_exp2f(p[3] * 2.6f), 0.7f);
        for (i = 0; i < n; i++) {
            float m = 0.0f, ns = pk_noise(v), bp, hp, y;
            for (k = 0; k < 6; k++) {
                float r = 1.0f + (pk_metal[k] - 1.0f) * gap;
                v->ph[k] += base * r * tr * RV_DT;
                v->ph[k] -= fm_floorf(v->ph[k]);
                m += v->ph[k] < 0.5f ? 1.0f : -1.0f;
            }
            y = m * (1.0f / 6.0f) * mtal + ns * (1.0f - mtal * 0.7f);
            svf_tick(&v->f1, y, &bp, &hp);              /* HPF (cymbal: PEAK sharpens it) */
            y = cy ? bp * (1.0f - top) + hp * top : hp;
            y = svf_tick(&v->f2, y, 0, 0);              /* LPF (cymbal: TTUN tunes the top) */
            out[i] = y * v->aenv * 1.3f;
            v->aenv *= ca;
            v->t += RV_DT;
        }
        break;
    }
    case PM_TRX_MA: {
        float att = pk_dec(p[0], 0.001f, 0.1f), sus = pk_dec(p[1], 0.01f, 0.5f), rev = p[2], damp = p[3];
        float ratl = p[4], rtyp = p[5], hard = p[7];
        svf_set(&v->f1, 2500.0f * fm_exp2f(p[6] * 2.5f), 1.0f + hard * 3.0f);
        svf_set(&v->f2, 1500.0f * fm_exp2f(rtyp * 3.0f), 6.0f);
        for (i = 0; i < n; i++) {
            float e, ns = pk_noise(v), hp, bp, y;
            if (v->t < att)
                e = rev > 0.5f ? (v->t / att) * (v->t / att) : v->t / att;
            else
                e = fm_expf(-(v->t - att) / sus) * (1.0f - rev * 0.5f);
            if (damp > 0.0f && (xs32(&v->rng) & 255u) < (uint32_t)(damp * 200.0f))
                ns *= 0.1f;                              /* DAMP: sparser grains */
            svf_tick(&v->f1, ns, 0, &hp);
            y = hp * e;
            if ((xs32(&v->rng) & 1023u) < (uint32_t)(ratl * 40.0f))
                v->xenv = 1.0f;                          /* RATL: rattles */
            svf_tick(&v->f2, pk_noise(v) * v->xenv, &bp, 0);
            y += bp * ratl * e * 0.8f;
            v->xenv *= 0.993f;
            out[i] = y * 1.2f;
            v->aenv = e;
            v->t += RV_DT;
        }
        break;
    }
    /* EFM: a carrier and a modulator, the modulator feeding back on itself */
    case PM_EFM_BD: case PM_EFM_SD: case PM_EFM_XT: case PM_EFM_CP: case PM_EFM_RS: case PM_EFM_CB: case PM_EFM_HH: case PM_EFM_CY: {
        int m = v->mach;
        static const float lo[8] = {28.0f, 90.0f, 55.0f, 200.0f, 200.0f, 300.0f, 400.0f, 300.0f};
        static const float hi_dec[8] = {2.5f, 0.8f, 2.0f, 0.6f, 0.4f, 1.2f, 1.5f, 4.0f};
        int e = m - PM_EFM_BD;
        float f0 = lo[e] * fm_exp2f(p[0] * 3.0f) * tr, ca = pk_coef(pk_dec(p[1], 0.02f, hi_dec[e]));
        float ramp = 0.0f, cp = 1.0f, modd, ratio, cm, fb = 0.0f, hpf = 0.0f, nois = 0.0f, cn = 1.0f, clic = 0.0f;
        float trem = 0.0f, tfrq = 0.0f, snar = 0.0f, sptc = 0.0f, cs = 1.0f, smod = 0.0f;
        int claps = 1;
        float cdec = 0.0f;
        modd = p[4] * 6.0f;
        ratio = 0.25f * fm_exp2f(p[5] * 6.0f);
        cm = pk_coef(pk_dec(p[6], 0.003f, 1.5f));
        switch (m) {
        case PM_EFM_BD: ramp = p[2] * 5.0f; cp = pk_coef(pk_dec(p[3], 0.004f, 0.25f)); fb = p[7]; break;
        case PM_EFM_SD: nois = p[2]; cn = pk_coef(pk_dec(p[3], 0.02f, 0.6f)); hpf = p[7]; break;
        case PM_EFM_XT: ramp = p[2] * 1.5f; cp = pk_coef(pk_dec(p[3], 0.005f, 0.4f)); clic = p[7]; break;
        case PM_EFM_CP: claps = 1 + (int)(p[2] * 6.0f); cdec = pk_dec(p[3], 0.003f, 0.03f); hpf = p[7]; break;
        case PM_EFM_RS: modd = p[2] * 6.0f; hpf = p[3]; snar = p[4]; sptc = 150.0f * fm_exp2f(p[5] * 3.0f) * tr;
                        cs = pk_coef(pk_dec(p[6], 0.01f, 0.4f)); smod = p[7] * 6.0f; ratio = 1.6f; cm = pk_coef(0.02f); break;
        case PM_EFM_CB: clic = p[2]; fb = p[3]; break;
        case PM_EFM_HH: trem = p[2]; tfrq = 2.0f * fm_exp2f(p[3] * 7.0f); fb = p[7]; break;
        default: fb = p[2]; hpf = p[3]; break;        /* CY */
        }
        for (i = 0; i < n; i++) {
            float f = f0 * (1.0f + ramp * v->penv), mo, y, ns;
            v->ph[1] += f * ratio * RV_DT;
            v->ph[1] -= fm_floorf(v->ph[1]);
            mo = pk_sin(v->ph[1] + fb * 0.5f * v->fbv);
            v->fbv = mo;
            v->ph[0] += f * RV_DT;
            v->ph[0] -= fm_floorf(v->ph[0]);
            y = pk_sin(v->ph[0] + modd * 0.16f * v->menv * mo);
            if (m == PM_EFM_CB || m == PM_EFM_CY || m == PM_EFM_HH)   /* the carrier's own feedback roughens it */
                y = pk_sin(v->ph[0] + modd * 0.16f * v->menv * mo + fb * 0.3f * v->last);
            v->last = y;
            if (m == PM_EFM_CP) {                       /* CLPS bursts, CDEC long */
                if (v->cnt < claps && v->t >= v->next) {
                    v->cnt++;
                    v->next = v->t + cdec + 0.004f;
                    v->xenv = 1.0f;
                }
                y *= v->xenv;
                v->xenv *= v->cnt < claps ? 0.995f : 1.0f;
            }
            if (snar > 0.0f) {                          /* RS: a second FM pair, the snare timbre */
                v->ph[2] += sptc * RV_DT;
                v->ph[3] += sptc * 2.31f * RV_DT;
                y += snar * pk_sin(v->ph[2] + smod * 0.16f * pk_sin(v->ph[3])) * v->nenv;
                v->nenv *= cs;
            }
            ns = pk_noise(v);
            if (nois > 0.0f) {
                y += ns * nois * v->nenv;
                v->nenv *= cn;
            }
            if (clic > 0.0f && v->t < 0.002f)
                y += clic * (ns * 0.5f + 0.8f);
            y *= v->aenv;
            if (trem > 0.0f)
                y *= 1.0f - trem * (0.5f + 0.5f * pk_sin(v->t * tfrq));
            if (m == PM_EFM_HH) {                       /* hats sit high: a fixed high-pass before HPF */
                v->hp_s2 += (y - v->hp_s2) * 0.25f;
                y -= v->hp_s2;
            }
            if (hpf > 0.0f) {                           /* HPF: a one-pole high-pass */
                float a = clampf(hpf * hpf * 0.9f, 0.0f, 0.95f);
                v->hp_s += (y - v->hp_s) * (1.0f - a);
                y -= v->hp_s * (0.3f + hpf * 0.7f);
            }
            out[i] = y * (m == PM_EFM_CP ? 1.9f : m == PM_EFM_CY ? 1.6f : m == PM_EFM_RS ? 1.4f : m == PM_EFM_HH ? 1.5f : 0.8f);
            v->penv *= cp;
            v->menv *= cm;
            v->aenv *= ca;
            v->t += RV_DT;
        }
        break;
    }
    /* P-I: modal bodies struck by a mallet */
    case PM_PI_BD: case PM_PI_SD: case PM_PI_MT: case PM_PI_RS: case PM_PI_ML: {
        static const float membrane[6] = {1.0f, 1.594f, 2.136f, 2.296f, 2.653f, 2.918f};
        static const float bar[6] = {1.0f, 2.756f, 5.404f, 8.933f, 13.34f, 18.64f};
        static const float wood[6] = {1.0f, 1.33f, 1.88f, 2.52f, 3.13f, 4.2f};
        int m = v->mach, nm = 6;
        const float *rat = m == PM_PI_ML ? bar : m == PM_PI_RS ? wood : membrane;
        float lo = m == PM_PI_BD ? 30.0f : m == PM_PI_SD ? 110.0f : m == PM_PI_MT ? 55.0f : m == PM_PI_RS ? 380.0f : 220.0f;
        float size = m == PM_PI_MT ? p[6] : 0.5f, dec = pk_dec(p[1], 0.06f, m == PM_PI_ML ? 5.0f : 2.0f) * (0.6f + size * 0.8f);
        float f0 = lo * fm_exp2f(p[0] * 2.5f - (size - 0.5f) * 1.2f) * tr, hard = p[2];
        float hamr = m == PM_PI_BD || m == PM_PI_MT ? p[3] : 0.3f;
        float tens = m == PM_PI_BD ? p[4] : m == PM_PI_SD ? p[4] : m == PM_PI_ML ? p[3] : 0.2f;
        float damp = m == PM_PI_BD ? p[5] : m == PM_PI_MT ? p[5] : 0.2f, tune = m == PM_PI_MT ? p[4] : 0.5f;
        float pos = m == PM_PI_MT ? p[7] : 0.2f, ring = m == PM_PI_SD || m == PM_PI_RS ? p[3] : 0.0f;
        float rvol = m == PM_PI_SD ? p[5] : m == PM_PI_RS ? p[4] : 0.0f, rdec = m == PM_PI_SD ? p[6] : m == PM_PI_RS ? p[5] : 0.0f;
        float glide = 1.0f + tens * 0.25f * v->penv, cutoff = 300.0f * fm_exp2f((1.0f - hamr) * 5.0f + hard * 2.0f);
        for (k = 0; k < nm; k++) {
            float r = rat[k];
            float w;
            if (rat == membrane)                       /* TUNE: from the ideal membrane toward a harmonic one */
                r = r + ((float)(k + 1) - r) * (tune - 0.5f) * 0.6f;
            w = k == 0 ? 1.0f - pos * 0.6f : (k & 1 ? 0.5f + pos : 0.6f - pos * 0.3f);   /* POS: centre to edge */
            pk_mode(v, k, f0 * r * glide, dec * (1.0f - damp * 0.8f) / (1.0f + (float)k * (0.35f + damp)),
                    w * (k == 0 ? 1.0f : 0.6f / (1.0f + (float)k * 0.3f)));
        }
        if (ring > 0.0f) {                              /* RING: metal that rings on (the snare's shell, the rim) */
            pk_mode(v, 6, f0 * 5.43f, dec * 1.5f, ring * 0.3f);
            pk_mode(v, 7, f0 * 7.91f, dec * 1.2f, ring * 0.2f);
            nm = 8;
        }
        svf_set(&v->f1, cutoff, 0.6f);
        svf_set(&v->f2, 2500.0f + rdec * 3000.0f, 0.8f);
        {
            float cx = pk_coef(0.0004f + hamr * 0.004f), cr = pk_coef(pk_dec(rdec, 0.03f, 0.5f)), cpn = pk_coef(0.04f);
            for (i = 0; i < n; i++) {
                float x = 0.0f, y, bp;
                if (v->exc > 0.0005f) {                  /* the mallet: a soft or hard burst */
                    x = (hard * pk_noise(v) * 0.6f + 1.0f) * v->exc * (0.6f + hard * 1.2f);
                    x = svf_tick(&v->f1, x, 0, 0);
                    v->exc *= cx;
                }
                y = pk_modes(v, nm, x);
                if (rvol > 0.0f) {                       /* the snare wires: noise that follows the drum */
                    svf_tick(&v->f2, pk_noise(v), &bp, 0);
                    y += bp * rvol * v->nenv * (0.4f + fm_fabsf(v->my1[0]) * 2.0f);
                    v->nenv *= cr;
                }
                out[i] = sat(y * 0.9f) * 0.7f;
                v->penv *= cpn;
                v->t += RV_DT;
            }
        }
        v->aenv = fm_fabsf(v->my1[0]) + fm_fabsf(v->my1[1]) + v->exc + (rvol > 0.0f ? v->nenv * rvol * 0.2f : 0.0f);
        break;
    }
    case PM_PI_MA: {
        float dec = pk_dec(p[0], 0.05f, 1.0f), grns = 200.0f + p[1] * 3000.0f, glen = pk_coef(pk_dec(p[2], 0.0005f, 0.01f));
        float body = 2500.0f * fm_exp2f((0.5f - p[3]) * 2.0f) * tr, hard = p[4];
        svf_set(&v->f1, body, 2.0f + hard * 4.0f);
        for (i = 0; i < n; i++) {
            float e = fm_expf(-v->t / dec), bp;
            if ((pk_noise(v) * 0.5f + 0.5f) < grns * RV_DT * e)
                v->xenv = 0.5f + 0.5f * (pk_noise(v) * 0.5f + 0.5f);   /* a grain hits */
            svf_tick(&v->f1, pk_noise(v) * v->xenv, &bp, 0);
            out[i] = (bp * (1.0f - hard * 0.4f) + pk_noise(v) * v->xenv * hard * 0.4f) * 1.6f;
            v->xenv *= glen;
            v->aenv = e;
            v->t += RV_DT;
        }
        break;
    }
    case PM_PI_HH: case PM_PI_RC: case PM_PI_CC: {
        static const float cym[8] = {1.0f, 1.417f, 1.876f, 2.271f, 2.745f, 3.304f, 3.832f, 4.587f};
        int m = v->mach;
        float lo = m == PM_PI_HH ? 3400.0f : m == PM_PI_RC ? 1400.0f : 900.0f;
        float f0 = lo * fm_exp2f((p[0] - 0.5f) * 1.5f) * tr, dec = pk_dec(p[1], 0.03f, m == PM_PI_HH ? 1.5f : 6.0f);
        float ringq = m == PM_PI_HH ? p[3] : p[3], hard = m == PM_PI_HH ? 0.5f : p[2], clsn = m == PM_PI_HH ? p[2] : 0.0f;
        float ag = p[4], au = p[5], br = p[6], grab = p[7], tgrab = grab >= 0.99f ? 1e9f : pk_dec(grab, 0.02f, 3.0f);
        if (v->t > tgrab)                               /* CLOS / GRAB: the hand stops it */
            dec = 0.02f;
        for (k = 0; k < 8; k++)
            pk_mode(v, k, f0 * cym[k] * (1.0f + 0.003f * (float)k), dec * (0.3f + ringq * 0.7f) / (1.0f + (float)k * 0.12f),
                    0.25f * (k < 3 ? 0.6f + br * 0.8f : 1.0f));
        svf_set(&v->f1, 4000.0f + ag * 6000.0f, 0.7f);
        svf_set(&v->f2, 16000.0f * fm_exp2f(-au * 2.5f), 0.7f);
        {
            float cx = pk_coef(0.0008f + (1.0f - hard) * 0.004f), cn = pk_coef(dec * 0.5f);
            for (i = 0; i < n; i++) {
                float x = 0.0f, y, hp, ns = pk_noise(v);
                if (v->exc > 0.0005f) {
                    x = ns * v->exc * (0.8f + hard);
                    v->exc *= cx;
                }
                y = pk_modes(v, 8, x) + ns * v->nenv * (0.15f + clsn * 0.4f);   /* the wash; CLSN: the two cymbals touching */
                if (clsn > 0.0f)
                    y *= 1.0f - clsn * 0.5f * (ns * 0.5f + 0.5f);
                svf_tick(&v->f1, y, 0, &hp);
                y += hp * (ag - 0.3f);                  /* AG: the highs up */
                y = svf_tick(&v->f2, y, 0, 0);          /* AU: the highs down */
                out[i] = sat(y * 1.2f);
                v->nenv *= cn;
                v->t += RV_DT;
            }
        }
        v->aenv = v->nenv + v->exc + fm_fabsf(v->my1[0]) + fm_fabsf(v->my1[3]);
        break;
    }
    case PM_GND_SN: {
        float f0 = 30.0f * fm_exp2f(p[0] * 7.0f) * tr, ca = pk_coef(pk_dec(p[1], 0.01f, 4.0f)), ramp = p[2] * 4.0f;
        float cp = pk_coef(pk_dec(p[3], 0.003f, 0.5f));
        for (i = 0; i < n; i++) {
            v->ph[0] += f0 * (1.0f + ramp * v->penv) * RV_DT;
            v->ph[0] -= fm_floorf(v->ph[0]);
            out[i] = pk_sin(v->ph[0]) * v->aenv;
            v->aenv *= ca;
            v->penv *= cp;
            v->t += RV_DT;
        }
        break;
    }
    case PM_GND_NS: {
        float ca = pk_coef(pk_dec(p[0], 0.005f, 4.0f));
        for (i = 0; i < n; i++) {
            out[i] = pk_noise(v) * v->aenv * 0.8f;
            v->aenv *= ca;
            v->t += RV_DT;
        }
        break;
    }
    default: {                                         /* GND-IM: up for UP at UVAL, down for DOWN at DVAL */
        float up = p[0] * p[0] * 0.02f + 0.00005f, dn = p[2] * p[2] * 0.02f + 0.00005f;
        for (i = 0; i < n; i++) {
            out[i] = v->t < up ? p[1] : v->t < up + dn ? -p[3] : 0.0f;
            v->t += RV_DT;
        }
        v->aenv = v->t < up + dn ? 1.0f : 0.0f;
        break;
    }
    }
}

/* each machine's output trim: every machine at its defaults hits about as hard as the others
 * (measured: the loudest 50 ms of one hit), within 8 dB */
static const float perc_trim[PM_N] = {1.000f, 0.785f, 0.822f, 0.851f, 1.334f, 1.175f, 1.429f, 2.512f, 1.462f, 1.995f, 1.135f, 1.122f, 0.794f, 1.245f, 1.622f, 1.000f, 1.288f, 1.216f, 1.000f, 0.851f, 0.813f, 1.000f, 0.692f, 0.759f, 0.684f, 0.684f, 1.585f, 0.716f, 0.589f, 0.589f, 0.785f, 1.274f, 2.512f};

/* the voice: machine, then the track effects (AM, EQ, the band filter), into out; returns its level */
RV_HOT2 static float perc_render(perc_t *v, track_t *tr, float *out, int n)
{
    const float *eff = tr->eff;
    float buf[RV_SUB], p[8], amd, amf, eqf, eqg, flo, fhi, fq, peak = 0.0f, velamt, g;
    int i, k;
    if (v->lane >= 0) {                               /* a kit lane: its own machine and settings */
        const rv_kit *kt = &rv.kit[v->track & 7];
        for (k = 0; k < 8; k++)
            p[k] = ((v->lk_mask >> k) & 1 ? v->lk_val[k] : kt->p[v->lane][k]) * (1.0f / 127.0f);
        perc_machine(v, p, v->hs.semi, buf, n);   /* (a lane plays at its own pitch; R MOVE and GRAIN move it) */
    } else {
        for (k = 0; k < 8; k++)
            p[k] = clampf(eff[TP_S1 + k] * (1.0f / 127.0f), 0.0f, 1.0f);
        perc_machine(v, p, (float)v->note - 60.0f + eff[TP_PITCH] + eff[TP_FINE] * 0.01f + v->hs.semi, buf, n);
    }
    /* the track effects */
    amd = eff[TP_S9] * (1.0f / 127.0f);
    amf = 0.1f * fm_exp2f(eff[TP_S10] * (14.5f / 127.0f));   /* 0.1 Hz .. 2.3 kHz */
    eqf = 40.0f * fm_exp2f(eff[TP_S11] * (8.9f / 127.0f));
    eqg = eff[TP_S12] * (18.0f / 64.0f);              /* dB */
    flo = eff[TP_S13] < 0.5f ? 0.0f : 20.0f * fm_exp2f(eff[TP_S13] * (10.0f / 127.0f));
    fhi = eff[TP_S14] > 126.5f ? 0.0f : 20.0f * fm_exp2f(clampf(eff[TP_S13] + eff[TP_S14], 0.0f, 127.0f) * (10.0f / 127.0f));
    fq = 0.5f + eff[TP_S15] * eff[TP_S15] * (12.0f / 16129.0f);
    if (flo > 0.0f)
        svf_set(&v->hpf, flo, fq);
    if (fhi > 0.0f)
        svf_set(&v->lpf, fhi, fq);
    if (eqg > 0.2f || eqg < -0.2f)
        svf_set(&v->eq, eqf, 1.2f);
    velamt = eff[TP_VELO] * (1.0f / 127.0f);
    g = (1.0f - velamt + velamt * v->vel) * 0.7f * perc_trim[v->mach % PM_N];
    if (v->lane >= 0)
        g *= ((v->lk_mask >> KL_LEVEL) & 1 ? v->lk_val[KL_LEVEL] : rv.kit[v->track & 7].level[v->lane]) * (1.0f / 100.0f);
    for (i = 0; i < n; i++) {
        float y = buf[i], bp, hp;
        if (amd > 0.0f) {
            v->am_ph += amf * RV_DT;
            v->am_ph -= fm_floorf(v->am_ph);
            y *= 1.0f - amd * (0.5f - 0.5f * pk_sin(v->am_ph));
        }
        if (eqg > 0.2f || eqg < -0.2f) {              /* a one-band peaking EQ: add or take away the band */
            svf_tick(&v->eq, y, &bp, 0);
            y += bp * v->eq.k * (fm_exp2f(eqg * 0.1661f) - 1.0f);
        }
        if (flo > 0.0f) {
            svf_tick(&v->hpf, y, 0, &hp);
            y = hp;
        }
        if (fhi > 0.0f)
            y = svf_tick(&v->lpf, y, 0, 0);
        y *= g;
        out[i] += y;
        if (fm_fabsf(y) > peak)
            peak = fm_fabsf(y);
    }
    v->level = fm_maxf(peak, v->level * 0.9f);
    /* done when the drum has died away */
    if (v->t > 0.02f && v->aenv < 0.0005f && peak < 0.0002f)
        v->on = 0;
    if (v->t > 30.0f)
        v->on = 0;
    return clampf(v->level * 2.0f, 0.0f, 1.0f);
}
