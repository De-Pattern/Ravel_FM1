/* SPDX-License-Identifier: GPL-3.0-only */
/* Ravel: the sound bank. 32 of your own sounds, any kind (synth of any engine, strike, perc single
 * or a kit), each with a name, kept in their own flash slot beside the projects and shared by every
 * project. A sound is a track's voice: its kind and engine and all its sound settings, the CZ patch,
 * the kit's lanes; not its steps, sequencer settings or pan. Loading one into a track of another
 * kind turns the track into that kind. Kept in RAM outside the pool (RV_RAMBSS). */

#define RV_BANK_N 32                               /* sounds in a bank */
#define RV_BANKS PLAT_BANKS                        /* banks A..D */
#define RV_BANK_MAGIC 0x4B4E4252u                  /* "RBNK" */
#define RV_BANK_NAME 12

/* a sound: a CZ patch or a kit's lanes, never both (the one its kind and engine use) */
typedef struct {
    char name[RV_BANK_NAME];
    uint8_t used, kind, engine, rsv;
    int16_t tp[RV_TP];
    union { rv_cz cz; rv_kit kit; } x;
} rv_sound;
typedef struct {
    uint32_t magic, version;                       /* version 2: a bank of four; 1: the single first bank */
    rv_sound s[RV_BANK_N];
} rv_bank_t;
/* the first bank's old shape (version 1), read once to move it to bank A */
typedef struct {
    char name[RV_BANK_NAME];
    uint8_t used, kind, engine, rsv;
    int16_t tp[RV_TP];
    rv_cz cz;
    rv_kit kit;
} rv_sound_v1;
typedef struct { uint32_t magic, version; rv_sound_v1 s[RV_BANK_N]; } rv_bank_v1;
/* one bank in RAM at a time (outside the pool), with room to read the old one */
static union { rv_bank_t b; rv_bank_v1 old; } bankmem RV_RAMBSS;
#define bank bankmem.b
static uint8_t bank_cur, bank_dirty;               /* the bank in RAM; a name edited and not yet kept */

static int bank_is_kit(int kind, int engine) { return kind == KIND_PERC && engine == 1; }
static int bank_is_cz(int kind, int engine) { return kind == KIND_SYNTH && engine == ENG_CZ; }

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

static void bank_blank(void)
{
    __builtin_memset(&bankmem, 0, sizeof bankmem);
    bank.magic = RV_BANK_MAGIC;
    bank.version = 2;
}

/* the old single bank, in RAM as version 1: reshaped in place into version 2 (each sound is
 * smaller and starts no later, so walking up never overwrites one not yet moved) */
static void bank_from_v1(void)
{
    int i;
    for (i = 0; i < RV_BANK_N; i++) {
        rv_sound_v1 o = bankmem.old.s[i];
        rv_sound *n = &bank.s[i];
        __builtin_memset(n, 0, sizeof *n);
        __builtin_memcpy(n->name, o.name, RV_BANK_NAME);
        n->used = o.used, n->kind = o.kind, n->engine = o.engine;
        __builtin_memcpy(n->tp, o.tp, sizeof n->tp);
        if (bank_is_kit(o.kind, o.engine))
            n->x.kit = o.kit;
        else
            n->x.cz = o.cz;
    }
    __builtin_memset((uint8_t *)&bankmem + sizeof bank, 0, sizeof bankmem - sizeof bank);
    bank.magic = RV_BANK_MAGIC;
    bank.version = 2;
}

static int bank_read(int b)
{
    return plat_load(PLAT_BANK0 + b, &bank, sizeof bank) == (int)sizeof bank && bank.magic == RV_BANK_MAGIC && bank.version == 2;
}

int rv_bank_flush(void)
{
    bank_dirty = 0;
    return plat_save(PLAT_BANK0 + bank_cur, &bank, sizeof bank);
}

/* bring bank b (0..3 = A..D) into RAM (a name edited in the one leaving is kept first) */
void rv_bank_select(int b)
{
    b = clampi(b, 0, RV_BANKS - 1);
    if (b == bank_cur)
        return;
    if (bank_dirty)
        rv_bank_flush();
    bank_cur = (uint8_t)b;
    if (!bank_read(b))
        bank_blank();
}
int rv_bank_current(void) { return bank_cur; }

void rv_bank_init(void)
{
    bank_cur = 0;
    if (bank_read(0))
        return;
    if (plat_load(PLAT_BANK_OLD, &bankmem.old, sizeof bankmem.old) == (int)sizeof bankmem.old &&
        bankmem.old.magic == RV_BANK_MAGIC && bankmem.old.version == 1) {
        bank_from_v1();                                /* the first bank moves to A */
        rv_bank_flush();
        return;
    }
    bank_blank();
}

int rv_bank_used(int i) { return i >= 0 && i < RV_BANK_N && bank.s[i].used; }
const char *rv_bank_name(int i) { return i >= 0 && i < RV_BANK_N ? bank.s[i].name : ""; }
int rv_bank_kind(int i) { return i >= 0 && i < RV_BANK_N ? bank.s[i].kind : 0; }
int rv_bank_engine(int i) { return i >= 0 && i < RV_BANK_N ? bank.s[i].engine : 0; }
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
    if (bank_is_kit(rv.tp[t & 7][TP_KIND], rv.tp[t & 7][TP_ENGINE]))
        s->x.kit = rv.kit[t & 7];
    else
        s->x.cz = rv.cz[t & 7];
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
    if (bank_is_kit(s->kind, s->engine))
        rv.kit[t & 7] = s->x.kit;
    else if (bank_is_cz(s->kind, s->engine))
        rv.cz[t & 7] = s->x.cz;
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
    bank_dirty = 1;
    for (k = RV_BANK_NAME - 2; k >= 0 && (bank.s[i].name[k] == ' ' || !bank.s[i].name[k]); k--)
        bank.s[i].name[k] = 0;                         /* trailing spaces go */
}
