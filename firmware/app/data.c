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
       NP };
#define P_ROOM 40                /* values room in a stored sound: NP can grow to it without moving the bank */

/* the settings' values (stored: append only) */
enum { G_CH, G_TRS, G_VOL, G_OCT, G_SLOT, G_SPEED, G_DIR, G_DECAY, G_WEAR, G_BRAKE, NG };
#define G_ROOM 32

#define NSND 16                  /* sound slots */
#define NAME_LEN 12
enum { ENG_SAW, ENG_FM };

/* how a value reads on the screen */
enum { F_NUM, F_SEC10, F_SUBOCT, F_RATIO, F_CAB, F_HZ, F_CH, F_ONOFF, F_SPEED, F_DIR, F_OCT };

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
};

typedef struct {
    char name[NAME_LEN];         /* printable ASCII, space padded, no terminator */
    uint8_t engine;              /* ENG_* */
    uint8_t np;                  /* values stored in p[] */
    int16_t p[P_ROOM];
} snd_t;

static struct {
    snd_t s[NSND];
} bank;                          /* stored in slot 1 */

static struct {
    uint8_t ng, pad[3];          /* values stored in g[] */
    int16_t g[G_ROOM];
} set;                           /* stored in slot 0; the ISR reads G_CH, G_TRS, G_VOL, G_OCT (int16: torn-free) */

static snd_t cur;                /* the sound playing (the edit buffer): PRESETS loads it, SAVE stores it */

#define SLOT_SET 0u
#define SLOT_BANK 1u

static int16_t clamp16(int32_t v, int32_t lo, int32_t hi) { return (int16_t)(v < lo ? lo : v > hi ? hi : v); }

static void snd_default(snd_t *s, uint32_t slot)
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
}

static void snd_check(snd_t *s, uint32_t slot)   /* a sound read from flash: keep what is valid, default the rest */
{
    snd_t d;
    uint32_t i, n = s->np < NP ? s->np : NP, ok = 1;
    snd_default(&d, slot);
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
    uint32_t i, k;
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
    n = store_load(SLOT_BANK, APP_ID, &bank, sizeof bank);
    k = n > 0 ? (uint32_t)n / sizeof(snd_t) : 0u;   /* whole sounds stored */
    for (i = 0; i < NSND; i++) {
        if (i < k)
            snd_check(&bank.s[i], i);
        else
            snd_default(&bank.s[i], i);
    }
    cur = bank.s[set.g[G_SLOT]];
}

static int data_save(uint32_t slot)              /* main loop, ~100 ms (two records, the audio fades): 0 = saved */
{
    int r;
    bank.s[slot] = cur;
    set.g[G_SLOT] = (int16_t)slot;
    r = store_save(SLOT_BANK, APP_ID, &bank, sizeof bank);
    r |= store_save(SLOT_SET, APP_ID, &set, sizeof set);
    return r;
}

static int snd_dirty(void) { return memcmp(&cur, &bank.s[set.g[G_SLOT]], sizeof cur) != 0; }

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

static void par_text(char *b, uint32_t n, int32_t v, uint32_t fmt)   /* n >= 16 */
{
    static const char *const CAB[3] = {"OFF", "4X12", "4X12 DARK"};
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
