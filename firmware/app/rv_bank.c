/* SPDX-License-Identifier: GPL-3.0-only */
/* Ravel: the sound bank. 32 of your own sounds, any kind (synth of any engine, strike, perc single
 * or a kit), each with a name, kept in their own flash slot beside the projects and shared by every
 * project. A sound is a track's voice: its kind and engine and all its sound settings, the CZ patch,
 * the kit's lanes; not its steps, sequencer settings or pan. Loading one into a track of another
 * kind turns the track into that kind. Kept in RAM outside the pool (RV_RAMBSS). */

#define RV_BANK_N 32
#define RV_BANK_MAGIC 0x4B4E4252u                  /* "RBNK" */
#define RV_BANK_NAME 12

typedef struct {
    char name[RV_BANK_NAME];
    uint8_t used, kind, engine, rsv;
    int16_t tp[RV_TP];
    rv_cz cz;
    rv_kit kit;
} rv_sound;
typedef struct {
    uint32_t magic, version;
    rv_sound s[RV_BANK_N];
} rv_bank_t;
static rv_bank_t bank RV_RAMBSS;

/* the settings that make a sound (the rest belong to the track's place in the song) */
static int bank_sound_param(int p)
{
    switch (p) {
    case TP_PAN:
    case TP_LEN: case TP_RATE: case TP_DIR: case TP_INTERVAL: case TP_CLKSRC: case TP_BRANCH:
    case TP_H2ROLE: case TP_H2RATE: case TP_H2START: case TP_SEQMODE:
        return 0;
    default:
        return !(p >= TP_H1ROLE && p <= TP_H2TRANS);
    }
}

void rv_bank_init(void)
{
    if (plat_load(PLAT_BANK, &bank, sizeof bank) != (int)sizeof bank || bank.magic != RV_BANK_MAGIC) {
        __builtin_memset(&bank, 0, sizeof bank);
        bank.magic = RV_BANK_MAGIC;
        bank.version = 1;
    }
}

int rv_bank_flush(void) { return plat_save(PLAT_BANK, &bank, sizeof bank); }

int rv_bank_used(int i) { return i >= 0 && i < RV_BANK_N && bank.s[i].used; }
const char *rv_bank_name(int i) { return i >= 0 && i < RV_BANK_N ? bank.s[i].name : ""; }
int rv_bank_kind(int i) { return i >= 0 && i < RV_BANK_N ? bank.s[i].kind : 0; }
int rv_bank_engine(int i) { return i >= 0 && i < RV_BANK_N ? bank.s[i].engine : 0; }
static int bank_is_kit(int kind, int engine) { return kind == KIND_PERC && engine == 1; }
int rv_bank_is_kit(int i) { return rv_bank_used(i) && bank_is_kit(bank.s[i].kind, bank.s[i].engine); }

/* a sound's kind, for lists: SYNTH, STRIKE, TAPE, PERC or KIT */
const char *rv_bank_kind_label(int i)
{
    static const char *const k[4] = {"SYNTH", "STRIKE", "TAPE", "PERC"};
    if (!rv_bank_used(i))
        return "";
    if (rv_bank_is_kit(i))
        return "KIT";
    return k[bank.s[i].kind & 3];
}

/* keep track t's sound in slot i, named name (0: a name from the track's sound) */
int rv_bank_store(int t, int i, const char *name)
{
    rv_sound *s;
    int k;
    if (i < 0 || i >= RV_BANK_N)
        return -1;
    s = &bank.s[i];
    __builtin_memcpy(s->tp, rv.tp[t & 7], sizeof s->tp);
    s->cz = rv.cz[t & 7];
    s->kit = rv.kit[t & 7];
    s->kind = (uint8_t)rv.tp[t & 7][TP_KIND];
    s->engine = (uint8_t)rv.tp[t & 7][TP_ENGINE];
    s->used = 1;
    if (name) {
        for (k = 0; k < RV_BANK_NAME - 1 && name[k]; k++)
            s->name[k] = name[k];
        for (; k < RV_BANK_NAME; k++)
            s->name[k] = 0;
    }
    return rv_bank_flush();
}

/* put slot i's sound on track t (its kind with it); the caller has dealt with steps that a change
 * between a kit and notes would make meaningless */
int rv_bank_apply(int t, int i)
{
    const rv_sound *s;
    int p, was_kit = bank_is_kit(rv.tp[t & 7][TP_KIND], rv.tp[t & 7][TP_ENGINE]);
    if (!rv_bank_used(i))
        return -1;
    s = &bank.s[i];
    rv_all_off();
    for (p = 0; p < TP_N; p++)
        if (bank_sound_param(p))
            rv.tp[t & 7][p] = s->tp[p];
    rv.cz[t & 7] = s->cz;
    rv.kit[t & 7] = s->kit;
    if (was_kit != bank_is_kit(s->kind, s->engine)) {  /* lanes and notes don't read each other's steps */
        int pt = rv.pat_cur[t & 7];
        for (rv.pat_cur[t & 7] = 0; rv.pat_cur[t & 7] < RV_PATTERNS; rv.pat_cur[t & 7]++)
            rv_pattern_clear(t);
        rv.pat_cur[t & 7] = (uint8_t)pt;
    }
    return 0;
}

int rv_bank_clear(int i)
{
    if (i < 0 || i >= RV_BANK_N)
        return -1;
    __builtin_memset(&bank.s[i], 0, sizeof bank.s[i]);
    return rv_bank_flush();
}

/* naming: the characters a name can use, and one character set in place (no flash write: the
 * caller flushes when the naming pauses) */
static const char bank_chars[] = " ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-+#&'.";
int rv_bank_char_index(char c)
{
    int k;
    for (k = 0; bank_chars[k]; k++)
        if (bank_chars[k] == c)
            return k;
    return 0;
}
int rv_bank_nchars(void) { return (int)sizeof bank_chars - 1; }
void rv_bank_set_char(int i, int pos, int ci)
{
    int k;
    if (!rv_bank_used(i) || pos < 0 || pos >= RV_BANK_NAME - 1)
        return;
    for (k = 0; k < pos; k++)                          /* (no holes before the cursor) */
        if (!bank.s[i].name[k])
            bank.s[i].name[k] = ' ';
    bank.s[i].name[pos] = bank_chars[clampi(ci, 0, rv_bank_nchars() - 1)];
    for (k = RV_BANK_NAME - 2; k >= 0 && (bank.s[i].name[k] == ' ' || !bank.s[i].name[k]); k--)
        bank.s[i].name[k] = 0;                         /* trailing spaces go */
}
