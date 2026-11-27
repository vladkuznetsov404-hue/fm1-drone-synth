/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Vlad Kuznetsov */
/* MASSA's data: the parameters of a sound and of the settings, their ranges and how they read on the screen, and the
 * two records in flash (docs/SPEC.md "What is stored"):
 *   slot 0  the settings: MIDI channel, TRS MIDI IN, volume, octave, last sound, the cassette's values
 *   slot 1  the bank: 16 sounds of 94 bytes (1 504 of the store's 4 076)
 * Both are arrays of int16 values indexed by the enums below, each with its own count, so a newer firmware appends
 * values (never reorders, never reuses an index) and reads an older record with the missing ones at their defaults.
 * Every value read back is checked against its range: another firmware, an older version or a damaged sector must
 * never put the engine somewhere it cannot be. */

/* a sound's values (stored: append only) */
enum { P_MASSA, P_BEATS, P_DARK, P_BREATH,                 /* HOME: the four macros */
       P_LAYERS, P_SUB, P_SUBOCT, P_CURVE,                 /* EDIT, SAW */
       P_RATIO, P_FB, P_INDEX,                             /* EDIT, FM (its 4th cell is P_SUB as well) */
       P_DRIVE, P_TONE, P_CAB, P_LEVEL,                    /* FX */
       P_ATTACK, P_RELEASE,                                /* ENV */
       P_RATE, P_TOPITCH, P_TOCUT, P_TOLEVEL,              /* LFO */
       P_CHORUS, P_REVERB,                                 /* FX, SHOEGAZE (the chorus and the reverb) */
       NP };
#define P_ROOM 40                /* values room in a stored sound: NP can grow to it without moving the bank */

/* the settings' values (stored: append only) */
enum { G_CH, G_TRS, G_VOL, G_OCT, G_SLOT, G_SPEED, G_DIR, G_DECAY, G_WEAR, G_BRAKE,
       G_DT, G_DFB, G_DMIX, G_DHEADS, G_DWOB, G_DDULL,     /* the tape echo: TIME, REPEATS, MIX, HEADS (a mask), WOBBLE, DULL */
       G_PITCH, G_LOCK,                                    /* the cassette: pitch apart from speed */
       G_DGLIDE, G_DDRV,                                   /* the echo's motor and its drive */
       G_GMIX, G_GSIZE, G_GDENS, G_GPITCH, G_GSCAT, G_GREV, G_GFB,   /* the grains */
       G_SCALE, G_ROOT, G_CHORD, G_LOW,                    /* SCL: the scale of the keys, the chords */
       G_MODE,                                             /* HARSH or SHOEGAZE */
       G_DROP,                                             /* the cassette's dropouts */
       G_SLIDE,                                            /* the notes' glide when a chord changes (tenths of a second) */
       NG };
#define G_ROOM 48

#define NSND 16                  /* sound slots */
#define NAME_LEN 12
enum { ENG_SAW, ENG_FM };

/* how a value reads on the screen */
enum { F_NUM, F_SEC10, F_SUBOCT, F_RATIO, F_CAB, F_HZ, F_CH, F_ONOFF, F_SPEED, F_DIR, F_OCT, F_MS, F_HEADS, F_PITCH,
       F_MODE, F_SCALE, F_ROOT, F_CHORD, F_LOW, F_GMS };
enum { MODE_HARSH, MODE_SHOE };

typedef struct {
    const char *label;
    int16_t lo, hi, def;
    uint8_t fmt;
} par_t;

static const par_t SP[NP] = {
    [P_MASSA] = {"MASSA", 0, 100, 40, F_NUM},      [P_BEATS] = {"BEATS", 0, 100, 30, F_NUM},
    [P_DARK] = {"DARK", 0, 100, 40, F_NUM},        [P_BREATH] = {"BREATH", 0, 100, 30, F_NUM},
    [P_LAYERS] = {"LAYERS", 1, 3, 3, F_NUM},       [P_SUB] = {"SUB LEVEL", 0, 100, 50, F_NUM},
    [P_SUBOCT] = {"SUB OCTAVE", 1, 2, 1, F_SUBOCT}, [P_CURVE] = {"SPREAD CURVE", 0, 100, 50, F_NUM},
    [P_RATIO] = {"RATIO", 1, 8, 1, F_RATIO},       [P_FB] = {"FEEDBACK", 0, 100, 30, F_NUM},
    [P_INDEX] = {"INDEX", 0, 100, 40, F_NUM},
    [P_DRIVE] = {"FUZZ DRIVE", 0, 100, 50, F_NUM}, [P_TONE] = {"FUZZ TONE", 0, 100, 50, F_NUM},
    [P_CAB] = {"CABINET", 0, 2, 1, F_CAB},         [P_LEVEL] = {"LEVEL", 0, 100, 80, F_NUM},
    [P_ATTACK] = {"SWELL", 0, 100, 20, F_SEC10},   /* 0 .. 10.0 s */
    [P_RELEASE] = {"RELEASE", 0, 200, 40, F_SEC10}, /* 0 .. 20.0 s */
    [P_RATE] = {"RATE", 1, 100, 10, F_HZ},         /* 0.01 .. 1.00 Hz */
    [P_TOPITCH] = {"TO PITCH", 0, 100, 20, F_NUM}, [P_TOCUT] = {"TO CUTOFF", 0, 100, 40, F_NUM},
    [P_TOLEVEL] = {"TO LEVEL", 0, 100, 20, F_NUM},
    [P_CHORUS] = {"SHIMMER", 0, 100, 0, F_NUM},    [P_REVERB] = {"BLOOM", 0, 100, 0, F_NUM},
};

static const par_t GP[NG] = {
    [G_CH] = {"MIDI CHANNEL", 0, 16, 0, F_CH},     /* 0: omni */
    [G_TRS] = {"TRS MIDI IN", 0, 1, 1, F_ONOFF},
    [G_VOL] = {"VOLUME", 0, 100, 80, F_NUM},
    [G_OCT] = {"OCTAVE", -3, 1, -2, F_OCT},
    [G_SLOT] = {"SOUND", 0, NSND - 1, 0, F_NUM},
    [G_SPEED] = {"SPEED", 50, 100, 100, F_SPEED},  /* percent: 50 = 1/2, 100 = 1, between = varispeed */
    [G_DIR] = {"DIRECTION", 0, 1, 0, F_DIR},
    [G_DECAY] = {"DECAY", 0, 100, 10, F_NUM},
    [G_WEAR] = {"WEAR", 0, 100, 30, F_NUM},
    [G_BRAKE] = {"BRAKE", 2, 40, 15, F_SEC10},     /* 0.2 .. 4.0 s */
    [G_DT] = {"TIME", 0, 100, 45, F_MS},           /* 60 .. 950 ms between the heads (dly_ms10) */
    [G_DFB] = {"REPEATS", 0, 100, 40, F_NUM},
    [G_DMIX] = {"ECHO", 0, 100, 0, F_NUM},         /* 0: off (the default: the instrument starts dry) */
    [G_DHEADS] = {"HEADS", 1, 7, 3, F_HEADS},      /* a mask: 1, 2, 4 = the heads at D, 2 D, 3 D */
    [G_DWOB] = {"WOBBLE", 0, 100, 25, F_NUM},
    [G_DDULL] = {"DULL", 0, 100, 40, F_NUM},
    [G_PITCH] = {"PITCH", -12, 12, 0, F_PITCH},    /* semitones, on top of SPEED (apart from it with LOCK) */
    [G_LOCK] = {"LOCK", 0, 1, 0, F_ONOFF},         /* ON: SPEED changes the time only, the pitch stays */
    [G_DGLIDE] = {"GLIDE", 0, 100, 30, F_NUM},     /* the echo's motor: 0.15 s .. 6 s across TIME's range */
    [G_DDRV] = {"TAPE DRIVE", 0, 100, 0, F_NUM},
    [G_GMIX] = {"GRAINS", 0, 100, 0, F_NUM},       /* 0: off */
    [G_GSIZE] = {"SIZE", 1, 100, 40, F_GMS},       [G_GDENS] = {"DENSITY", 1, 100, 30, F_NUM},
    [G_GPITCH] = {"GRAIN PITCH", 0, 100, 0, F_NUM}, [G_GSCAT] = {"SCATTER", 0, 100, 40, F_NUM},
    [G_GREV] = {"REVERSE", 0, 100, 20, F_NUM},     [G_GFB] = {"GRAIN FEED", 0, 100, 0, F_NUM},
    [G_SCALE] = {"SCALE", 0, 6, 0, F_SCALE},       [G_ROOT] = {"ROOT", 0, 11, 0, F_ROOT},
    [G_CHORD] = {"CHORD", 0, 7, 0, F_CHORD},       [G_LOW] = {"LOW ROOT", 0, 2, 0, F_LOW},
    [G_MODE] = {"MODE", 0, 1, 0, F_MODE},
    [G_DROP] = {"DROPOUT", 0, 100, 0, F_NUM},
    [G_SLIDE] = {"SLIDE", 0, 60, 15, F_SEC10},     /* the time constant of a note moving to the next chord: 0 = at once */
};

typedef struct {
    char name[NAME_LEN];         /* printable ASCII, space padded, no terminator */
    uint8_t engine;              /* ENG_* */
    uint8_t np;                  /* values stored in p[] */
    int16_t p[P_ROOM];
} snd_t;

static struct {
    snd_t s[NSND];
} bankm[2];                      /* the sounds of each mode: HARSH in slot 1, SHOEGAZE in slot 2 */
#define bank (bankm[set.g[G_MODE] ? 1 : 0])   /* the bank of the mode playing */

static struct {
    uint8_t ng, pad[3];          /* values stored in g[] */
    int16_t g[G_ROOM];
} set;                           /* stored in slot 0; the ISR reads G_CH, G_TRS, G_VOL, G_OCT (int16: torn-free) */

static snd_t cur;                /* the sound playing (the edit buffer): PRESETS loads it, SAVE stores it */

#define SLOT_SET 0u
#define SLOT_BANK 1u
#define SLOT_BANK2 2u

static int16_t clamp16(int32_t v, int32_t lo, int32_t hi) { return (int16_t)(v < lo ? lo : v > hi ? hi : v); }

/* The factory sounds of slots 2..16 (slot 1 is INIT: the values' defaults). Values in the order of P_*:
 * MASSA BEATS DARK BREATH | LAYERS SUB SUBOCT CURVE | RATIO FB INDEX | DRIVE TONE CAB LEVEL | SWELL RELEASE |
 * RATE TOPITCH TOCUT TOLEVEL (SWELL and RELEASE in tenths of a second, RATE in hundredths of a Hz). */
typedef struct {
    char name[NAME_LEN + 1];
    uint8_t engine;
    uint8_t p[NP];
} factory_t;

static const factory_t FACTORY[NSND - 1] = {
    {"MONOLITH", ENG_SAW, {80, 20, 55, 30, 3, 80, 1, 60, 1, 30, 40, 70, 40, 1, 80, 40, 80, 8, 20, 40, 20}},
    {"SUNN WALL", ENG_SAW, {100, 35, 45, 40, 3, 70, 1, 50, 1, 30, 40, 90, 55, 1, 80, 30, 60, 10, 20, 50, 30}},
    {"BEATING", ENG_SAW, {20, 60, 30, 20, 2, 30, 1, 80, 1, 30, 40, 20, 60, 1, 80, 20, 40, 10, 10, 30, 20}},
    {"DARK SUB", ENG_SAW, {50, 15, 85, 50, 3, 100, 2, 50, 1, 30, 40, 40, 40, 2, 80, 60, 120, 6, 20, 60, 30}},
    {"FM RUST", ENG_FM, {60, 25, 40, 30, 2, 50, 1, 50, 1, 70, 30, 60, 50, 1, 80, 30, 60, 10, 20, 40, 20}},
    {"BELL TOLL", ENG_FM, {20, 10, 30, 40, 2, 20, 1, 50, 3, 10, 60, 25, 60, 0, 75, 10, 120, 8, 30, 30, 40}},
    {"FM GROWL", ENG_FM, {80, 30, 50, 30, 2, 60, 1, 50, 2, 50, 70, 80, 50, 1, 80, 20, 60, 12, 20, 50, 20}},
    {"SOUR CHORUS", ENG_SAW, {40, 90, 35, 60, 3, 40, 1, 20, 1, 30, 40, 40, 60, 1, 80, 30, 60, 20, 40, 40, 30}},
    {"TIDE", ENG_SAW, {50, 30, 50, 100, 3, 50, 1, 50, 1, 30, 40, 50, 50, 1, 80, 50, 100, 5, 60, 100, 80}},
    {"GLASS", ENG_FM, {0, 15, 20, 40, 2, 10, 1, 50, 4, 0, 20, 0, 70, 0, 70, 30, 100, 10, 20, 30, 30}},
    {"TAR PIT", ENG_SAW, {90, 20, 90, 30, 3, 100, 2, 50, 1, 30, 40, 100, 20, 2, 80, 40, 80, 6, 20, 40, 20}},
    {"SWARM", ENG_SAW, {70, 100, 40, 70, 3, 40, 1, 0, 1, 30, 40, 60, 60, 1, 80, 20, 60, 60, 100, 50, 30}},
    {"ORGAN DOOM", ENG_FM, {50, 10, 40, 30, 2, 60, 1, 50, 2, 20, 30, 50, 50, 1, 80, 30, 60, 8, 20, 40, 20}},
    {"FIZZ", ENG_SAW, {100, 40, 10, 20, 3, 30, 1, 50, 1, 30, 40, 100, 100, 0, 70, 10, 40, 10, 20, 20, 20}},
    {"HUM", ENG_SAW, {10, 5, 60, 20, 1, 60, 1, 50, 1, 30, 40, 10, 40, 1, 80, 20, 40, 8, 10, 30, 20}},
};

/* SHOEGAZE's fifteen: the same order of values, then CHORUS and REVERB. Wide detuned layers, a bright open DARK, a
 * light fuzz, long swells, the chorus and the reverb doing the rest. */
static const factory_t FACTORY_S[NSND - 1] = {
    {"WALL OF HAZE", ENG_SAW, {30, 70, 25, 40, 3, 30, 1, 50, 1, 30, 40, 45, 65, 0, 80, 40, 100, 8, 20, 30, 30, 70, 80}},
    {"SLOW BLOOM", ENG_SAW, {20, 55, 15, 50, 3, 20, 1, 40, 1, 30, 40, 30, 70, 0, 80, 80, 120, 6, 30, 40, 40, 60, 90}},
    {"PINK DUST", ENG_FM, {25, 40, 20, 40, 2, 20, 1, 50, 2, 20, 35, 30, 75, 0, 80, 40, 90, 10, 20, 30, 30, 80, 70}},
    {"LILAC", ENG_SAW, {15, 85, 10, 60, 2, 10, 1, 30, 1, 30, 40, 20, 80, 0, 75, 50, 110, 7, 40, 50, 50, 90, 80}},
    {"GLASS FUZZ", ENG_SAW, {45, 60, 30, 30, 3, 25, 1, 50, 1, 30, 40, 75, 60, 0, 80, 20, 80, 12, 20, 30, 20, 50, 60}},
    {"SUNRISE", ENG_FM, {10, 30, 5, 70, 2, 10, 1, 50, 3, 10, 50, 10, 85, 0, 75, 100, 150, 5, 50, 60, 60, 70, 100}},
    {"TREMOLO SKY", ENG_SAW, {30, 50, 20, 90, 3, 20, 1, 50, 1, 30, 40, 40, 70, 0, 80, 40, 80, 40, 30, 20, 100, 60, 70}},
    {"DEEP BLUE", ENG_SAW, {55, 45, 45, 30, 3, 70, 2, 50, 1, 30, 40, 50, 55, 0, 80, 60, 120, 6, 20, 30, 30, 60, 90}},
    {"STATIC LOVE", ENG_FM, {35, 70, 25, 50, 2, 30, 1, 50, 2, 40, 55, 60, 65, 0, 80, 30, 70, 14, 30, 40, 30, 70, 60}},
    {"SATIN", ENG_SAW, {10, 90, 10, 40, 3, 10, 1, 20, 1, 30, 40, 15, 85, 0, 75, 60, 100, 9, 20, 30, 40, 100, 90}},
    {"VELVET NOISE", ENG_SAW, {70, 80, 35, 60, 3, 40, 1, 80, 1, 30, 40, 85, 55, 0, 80, 30, 90, 20, 40, 50, 30, 80, 80}},
    {"DREAMS", ENG_FM, {20, 35, 15, 60, 2, 15, 1, 50, 4, 20, 45, 20, 80, 0, 75, 90, 140, 8, 40, 50, 50, 90, 100}},
    {"HALO", ENG_SAW, {15, 60, 5, 40, 3, 15, 1, 50, 1, 30, 40, 25, 90, 0, 70, 70, 130, 7, 30, 40, 40, 80, 100}},
    {"MELT", ENG_SAW, {40, 100, 30, 80, 3, 30, 1, 0, 1, 30, 40, 55, 60, 0, 80, 40, 100, 25, 100, 50, 40, 70, 80}},
    {"AFTERGLOW", ENG_SAW, {25, 65, 20, 50, 3, 25, 1, 50, 1, 30, 40, 35, 70, 0, 80, 120, 200, 4, 20, 30, 30, 80, 100}},
};

static void snd_default_m(snd_t *s, uint32_t slot, uint32_t mode)
{
    static const char base[] = "INIT ";
    uint32_t i;
    for (i = 0; i < NAME_LEN; i++)
        s->name[i] = ' ';
    for (i = 0; base[i]; i++)
        s->name[i] = base[i];
    s->name[i] = (char)('0' + (slot + 1u) / 10u);
    s->name[i + 1u] = (char)('0' + (slot + 1u) % 10u);
    s->engine = ENG_SAW;
    s->np = NP;
    for (i = 0; i < P_ROOM; i++)
        s->p[i] = i < NP ? SP[i].def : 0;
    if (mode == MODE_SHOE && slot == 0u) {       /* SHOEGAZE's INIT: a light fuzz, a little chorus and reverb */
        s->p[P_DRIVE] = 30;
        s->p[P_CAB] = 0;
        s->p[P_CHORUS] = 50;
        s->p[P_REVERB] = 50;
        s->p[P_DARK] = 20;
    }
    if (slot >= 1u && slot < NSND) {             /* a factory sound */
        const factory_t *f = mode == MODE_SHOE ? &FACTORY_S[slot - 1u] : &FACTORY[slot - 1u];
        for (i = 0; i < NAME_LEN; i++)
            s->name[i] = ' ';
        for (i = 0; i < NAME_LEN && f->name[i]; i++)
            s->name[i] = f->name[i];
        s->engine = f->engine;
        for (i = 0; i < NP; i++)
            s->p[i] = f->p[i];
    }
}

static void snd_default(snd_t *s, uint32_t slot) { snd_default_m(s, slot, (uint32_t)set.g[G_MODE]); }

static void snd_check(snd_t *s, uint32_t slot, uint32_t mode)   /* a sound read from flash: keep what is valid, default the rest */
{
    snd_t d;
    uint32_t i, n = s->np < NP ? s->np : NP, ok = 1;
    snd_default_m(&d, slot, mode);
    for (i = 0; i < NAME_LEN; i++)
        ok &= s->name[i] >= ' ' && s->name[i] <= '~';
    if (!ok)
        for (i = 0; i < NAME_LEN; i++)
            s->name[i] = d.name[i];
    if (s->engine > ENG_FM)
        s->engine = ENG_SAW;
    for (i = 0; i < P_ROOM; i++)
        if (i >= n || s->p[i] < SP[i < NP ? i : 0].lo || s->p[i] > SP[i < NP ? i : 0].hi)
            s->p[i] = d.p[i];
    s->np = NP;
}

static void set_default(void)
{
    uint32_t i;
    set.ng = NG;
    for (i = 0; i < G_ROOM; i++)
        set.g[i] = i < NG ? GP[i].def : 0;
}

static void data_load(void)      /* app_init: defaults, then the records over them */
{
    int32_t n;
    uint32_t i, k, m;
    set_default();
    n = store_load(SLOT_SET, APP_ID, &set, sizeof set);
    if (n < 4) {                                 /* none, damaged, or another app's */
        set_default();
    } else {
        k = set.ng < NG ? set.ng : NG;
        if ((uint32_t)n < 4u + 2u * k)            /* (a record shorter than it says) */
            k = ((uint32_t)n - 4u) / 2u;
        for (i = 0; i < G_ROOM; i++)
            if (i >= k || set.g[i] < GP[i < NG ? i : 0].lo || set.g[i] > GP[i < NG ? i : 0].hi)
                set.g[i] = i < NG ? GP[i].def : 0;
        set.ng = NG;
    }
    for (m = 0; m < 2u; m++) {
        n = store_load(m ? SLOT_BANK2 : SLOT_BANK, APP_ID, &bankm[m], sizeof bankm[m]);
        k = n > 0 ? (uint32_t)n / sizeof(snd_t) : 0u;   /* whole sounds stored */
        for (i = 0; i < NSND; i++) {
            if (i < k)
                snd_check(&bankm[m].s[i], i, m);
            else
                snd_default_m(&bankm[m].s[i], i, m);
        }
    }
    cur = bank.s[set.g[G_SLOT]];
}

static int data_save(uint32_t slot)              /* main loop, ~100 ms (two records, the audio fades): 0 = saved */
{
    int r;
    bank.s[slot] = cur;
    set.g[G_SLOT] = (int16_t)slot;
    r = store_save(set.g[G_MODE] ? SLOT_BANK2 : SLOT_BANK, APP_ID, &bank, sizeof bank);   /* (the mode playing) */
    r |= store_save(SLOT_SET, APP_ID, &set, sizeof set);
    return r;
}

static int snd_dirty(void) { return memcmp(&cur, &bank.s[set.g[G_SLOT]], sizeof cur) != 0; }

/* the tape echo's TIME 0..100 as tenths of a ms between the heads: 60.0 .. 950.0 ms, quadratic (fine at the short end) */
static int32_t dly_ms10(int32_t t) { return 600 + t * t * 89 / 100; }

/* -------------------------------------------------------- values as text --- */
static void str_cat(char *d, const char *s, unsigned max)   /* append, d stays terminated within max */
{
    unsigned n = str_len(d);
    if (n < max)
        str_cpy(d + n, s, max - n);
}

static void num(char *b, int32_t v)              /* signed decimal */
{
    char t[12];
    uint32_t n = 0, u = v < 0 ? (uint32_t)-v : (uint32_t)v;
    if (v < 0)
        *b++ = '-';
    do
        t[n++] = (char)('0' + u % 10u);
    while ((u /= 10u) && n < sizeof t);
    while (n)
        *b++ = t[--n];
    *b = 0;
}

static void fixed2(char *b, int32_t v, uint32_t dec)   /* v / 10^dec with dec decimals (dec 1 or 2) */
{
    int32_t d = dec == 2u ? 100 : 10;
    num(b, v / d);
    b += str_len(b);
    *b++ = '.';
    if (dec == 2u)
        *b++ = (char)('0' + v % 100 / 10);
    *b++ = (char)('0' + v % 10);
    *b = 0;
}

static void note_pc(char *b, uint32_t pc)        /* a pitch class: "C", "C#" */
{
    static const char *const N[12] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
    str_cpy(b, N[pc % 12u], 4);
}

static void par_text(char *b, uint32_t n, int32_t v, uint32_t fmt)   /* n >= 16 */
{
    static const char *const CAB[3] = {"OFF", "4X12", "DARK"};
    (void)n;
    switch (fmt) {
    case F_SEC10: fixed2(b, v, 1); str_cat(b, " S", n); break;
    case F_SUBOCT: str_cpy(b, v == 2 ? "-2 OCT" : "-1 OCT", n); break;
    case F_RATIO: str_cpy(b, "X", n); num(b + 1, v); break;
    case F_CAB: str_cpy(b, CAB[v < 0 || v > 2 ? 0 : v], n); break;
    case F_HZ: fixed2(b, v, 2); str_cat(b, " HZ", n); break;
    case F_CH: if (v) num(b, v); else str_cpy(b, "OMNI", n); break;
    case F_ONOFF: str_cpy(b, v ? "ON" : "OFF", n); break;
    case F_SPEED:
        if (v <= 50) str_cpy(b, "1/2", n);
        else if (v >= 100) str_cpy(b, "1", n);
        else fixed2(b, v, 2);
        break;
    case F_DIR: str_cpy(b, v ? "REVERSE" : "FORWARD", n); break;
    case F_MS: num(b, dly_ms10(v) / 10); str_cat(b, " MS", n); break;
    case F_GMS: num(b, 20 + v * 6); str_cat(b, " MS", n); break;
    case F_PITCH: if (v > 0) { b[0] = '+'; num(b + 1, v); } else num(b, v); str_cat(b, " ST", n); break;
    case F_MODE: str_cpy(b, v ? "SHOEGAZE" : "HARSH", n); break;
    case F_SCALE: {
        static const char *const SC[7] = {"MANUAL", "MINOR", "PHRYGIAN", "LOCRIAN", "HARM MIN", "PENT MIN", "WHOLE"};
        str_cpy(b, SC[v < 0 || v > 6 ? 0 : v], n);
        break;
    }
    case F_ROOT: note_pc(b, (uint32_t)v); break;
    case F_CHORD: {
        static const char *const CH[8] = {"SINGLE", "POWER", "POWER+8", "SUS4", "MINOR", "TRITONE", "DOOM", "OCTAVES"};
        str_cpy(b, CH[v < 0 || v > 7 ? 0 : v], n);
        break;
    }
    case F_LOW: str_cpy(b, v == 2 ? "-2 OCT" : v == 1 ? "-1 OCT" : "OFF", n); break;
    case F_HEADS: {
        uint32_t k, m = 0;
        for (k = 0; k < 3u; k++)
            if ((v >> k) & 1)
                b[m++] = (char)('1' + k);
        b[m] = 0;
        break;
    }
    case F_OCT: if (v > 0) { b[0] = '+'; num(b + 1, v); } else num(b, v); break;
    default: num(b, v); break;
    }
}

static void note_name(char *b, uint32_t note)   /* MIDI note -> "C#2" (MIDI 60 = C4) */
{
    static const char *const N[12] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
    str_cpy(b, N[note % 12u], 4);
    num(b + str_len(b), (int32_t)(note / 12u) - 1);
}
