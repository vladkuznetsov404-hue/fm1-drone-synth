/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Vlad Kuznetsov */
/* The chain after the voices (audio ISR), milestone 3 of docs/SPEC.md: a DC block, the fuzz (gain, an asymmetric
 * cubic soft clip), a second DC block (the asymmetry makes DC), the fuzz's tone, the output level, the 4x12 cabinet
 * (a steep low-pass and the speakers' low hump: three biquads) and the DARK low-pass (two one-poles, 12 dB/oct). The MASSA macro drives the fuzz together with
 * FUZZ DRIVE and lifts the sub (fx.samp, read by the voices).
 *
 * Integer throughout (the platform's code is; no FPU, no soft float in the ISR): the shaper and the one-poles in
 * Q14, the biquads with Q28 coefficients and a 64-bit sum (DF1). Every coefficient is a constant worked out once
 * (tools: RBJ cookbook, FS 44 100), the per-value ones looked up in tables; fx_update runs only when a value it
 * uses changed (compared once per block), never per sample.
 *
 * Silence: IIR filters in integers can hang at a few LSB (limit cycles) and the DC blocks decay slowly, so once
 * the voices have been silent for FX_IDLE samples the states are cleared and the output is exact zero (and the
 * chain costs nothing while idle). The tails are long gone by then (the slowest, the DC block's, falls 60 dB in
 * ~0.15 s at R = 0.995... and the release itself has ended). */
#define FX_UNIT 16384            /* 1.0 in the shaper, Q14 */
#define FX_OUT 11000             /* the shaper's full output at LEVEL 100, Q15: room for the cabinet's +5 dB hump */
#define FX_IDLE 8192u            /* samples (~0.19 s) of silent voices before the chain is cleared */
#define SUB_AMP 4500             /* the sub's peak at SUB LEVEL 100 and MASSA 0, Q15 (MASSA 100 lifts it 60 %) */
#define DC_R 32604               /* the DC blocks' pole, Q15 (0.995: -3 dB at ~35 Hz) */
#define TONE_A 2176              /* the tone's one-pole, Q14: ~1 kHz (1 - exp(-2 pi 1000 / FS)) */

/* the cabinets, three biquads each, Q28 {b0, b1, b2, a1, a2} (a0 = 1): a 4th-order Butterworth low-pass (two
 * biquads, Q 0.54 and 1.31) and the speakers' low hump.
 * 4x12: low-pass 4.2 kHz (-4.5 dB at 4.5 k, -14 at 6 k, -25 at 8 k), +5 dB at 110 Hz (Q 0.9)
 * dark: low-pass 2.3 kHz (-10 dB at 3 k, -24 at 4.5 k), +6 dB at 100 Hz */
static const int32_t CAB[2][3][5] = {
    {{15327254, 30654508, 15327254, -291525833, 84399393},
     {19194807, 38389613, 19194807, -365087053, 173430823},
     {269790623, -533322938, 263597820, -533322938, 264952987}},
    {{5502336, 11004671, 5502336, -391598403, 145172289},
     {6360737, 12721474, 6360737, -452690374, 209697865},
     {269924148, -533825169, 263955208, -533825169, 265443900}},
};

/* DARK 0..100 -> the one-poles' coefficient, Q14: cutoff 12 kHz .. 150 Hz, even steps in pitch (exponential) */
static const uint16_t DARK_A[101] = {
    13420, 13194, 12963, 12725, 12482, 12234, 11983, 11728, 11470, 11209, 10947, 10684, 10420, 10156, 9892, 9629,
    9368, 9108, 8850, 8595, 8343, 8094, 7848, 7606, 7368, 7134, 6904, 6679, 6459, 6243, 6033, 5827, 5626, 5430,
    5240, 5054, 4873, 4698, 4527, 4362, 4201, 4045, 3894, 3748, 3607, 3470, 3337, 3209, 3086, 2966, 2851, 2739,
    2632, 2528, 2428, 2332, 2239, 2150, 2064, 1981, 1901, 1824, 1750, 1679, 1611, 1545, 1482, 1421, 1363, 1307,
    1253, 1201, 1152, 1104, 1058, 1014, 972, 932, 893, 856, 820, 786, 753, 721, 691, 662, 634, 607, 582, 557, 534,
    511, 490, 469, 449, 430, 412, 395, 378, 362, 346};

/* 0..48 dB -> a gain, Q8 */
static const uint16_t DB_Q8[49] = {
    256, 287, 322, 362, 406, 455, 511, 573, 643, 722, 810, 908, 1019, 1144, 1283, 1440, 1615, 1812, 2033, 2282,
    2560, 2872, 3223, 3616, 4057, 4552, 5108, 5731, 6430, 7215, 8095, 9083, 10192, 11435, 12830, 14396, 16153,
    18123, 20335, 22816, 25600, 28724, 32228, 36161, 40573, 45524, 51079, 57311, 64304};

enum { FV_DRIVE, FV_TONE, FV_CAB, FV_LEVEL, FV_MASSA, FV_SUB, FV_DARK, FV_N };

static struct {
    int16_t last[FV_N];          /* the values fx_update last used (-1: never) */
    int32_t gain;                /* the fuzz's input gain, Q8 */
    int32_t gainm;               /* ... with the LFO on it (breath.c, per block) */
    int32_t tone, level;         /* Q14 (0..1), Q15 at the output */
    int32_t dark_a;              /* the DARK one-poles' coefficient, Q14 (with BREATH's LFO: breath.c, per block) */
    int32_t lmod;                /* BREATH's level factor, Q15 (breath.c, per block) */
    int32_t samp;                /* the sub's level for the voices, Q15 */
    uint8_t cab;                 /* 0 off, 1 4x12, 2 4x12 dark */
    uint8_t bypass;              /* the voices alone, no chain (the host tests of the engines) */
    uint32_t idle;               /* samples the voices have been silent */
    int32_t dc1x, dc1y, dc2x, dc2y, tlp, lp1, lp2;
    int32_t bq[3][4];            /* the cabinet's states: x1, x2, y1, y2 */
} fx = {.last = {-1, -1, -1, -1, -1, -1, -1}, .lmod = 32768};

static int32_t fx_pole(int32_t y)   /* y * DC_R / 32768, rounded toward zero: the DC blocks decay all the way to 0 */
{
    int32_t t = y * DC_R;
    return (t + (t < 0 ? 32767 : 0)) >> 15;
}

/* The values -> the chain's settings. Fuzz: FUZZ DRIVE and MASSA each add up to 25 dB (48 dB at most, ~250x): MASSA
 * 40 and DRIVE 50 (the defaults) give ~22 dB, a thick fuzz; both at 0 a light saturation of the loudest chords. The
 * sub: SUB LEVEL, lifted up to 60 % by MASSA (the "weight" of the macro). */
static __attribute__((noinline)) void fx_update(void)
{
    int32_t db = (cur.p[P_DRIVE] + cur.p[P_MASSA]) / 4;
    int32_t sub = cur.p[P_SUB] * (1000 + 6 * cur.p[P_MASSA]) / 1000;
    fx.last[FV_DRIVE] = cur.p[P_DRIVE];
    fx.last[FV_TONE] = cur.p[P_TONE];
    fx.last[FV_CAB] = cur.p[P_CAB];
    fx.last[FV_LEVEL] = cur.p[P_LEVEL];
    fx.last[FV_MASSA] = cur.p[P_MASSA];
    fx.last[FV_SUB] = cur.p[P_SUB];
    fx.last[FV_DARK] = cur.p[P_DARK];
    fx.gain = DB_Q8[db > 48 ? 48 : db < 0 ? 0 : db];
    fx.tone = cur.p[P_TONE] * FX_UNIT / 100;
    fx.level = cur.p[P_LEVEL] * FX_OUT / 100;
    fx.cab = (uint8_t)(cur.p[P_CAB] < 0 ? 0 : cur.p[P_CAB] > 2 ? 2 : cur.p[P_CAB]);
    fx.dark_a = DARK_A[cur.p[P_DARK] < 0 ? 0 : cur.p[P_DARK] > 100 ? 100 : cur.p[P_DARK]];
    fx.samp = SUB_AMP * sub / 100;
}

static void fx_check(void)       /* once per block: a value changed -> fx_update */
{
    if (fx.last[FV_DRIVE] != cur.p[P_DRIVE] || fx.last[FV_TONE] != cur.p[P_TONE] || fx.last[FV_CAB] != cur.p[P_CAB] ||
        fx.last[FV_LEVEL] != cur.p[P_LEVEL] || fx.last[FV_MASSA] != cur.p[P_MASSA] ||
        fx.last[FV_SUB] != cur.p[P_SUB] || fx.last[FV_DARK] != cur.p[P_DARK])
        fx_update();
}

static __attribute__((noinline)) void fx_clear(void)   /* every state to zero: exact silence from here */
{
    fx.dc1x = fx.dc1y = fx.dc2x = fx.dc2y = fx.tlp = fx.lp1 = fx.lp2 = 0;
    memset(fx.bq, 0, sizeof fx.bq);
}

static inline __attribute__((always_inline)) int32_t fx_biquad(int32_t x, const int32_t *c, int32_t *s)
{                                /* DF1, Q28 coefficients, a 64-bit sum (inlined: it runs 3 times a sample) */
    int64_t acc = (int64_t)c[0] * x + (int64_t)c[1] * s[0] + (int64_t)c[2] * s[1] - (int64_t)c[3] * s[2] -
                  (int64_t)c[4] * s[3];
    int32_t y = (int32_t)(acc >> 28);
    s[1] = s[0];
    s[0] = x;
    s[3] = s[2];
    s[2] = y;
    return y;
}

/* out[2i]: the voices' mono sum in, the chain's output out (Q15). active: a voice is sounding (or releasing). */
static void fx_process(int32_t *out, uint32_t n, int active)
{
    uint32_t i, k;
    int32_t lvl, gn;
    if (fx.bypass)
        return;
    if (!active) {
        if (fx.idle >= FX_IDLE) {                /* long silent: cleared, exact zero, nothing to compute */
            for (i = 0; i < n; i++)
                out[2u * i] = 0;
            return;
        }
        fx.idle += n;
        if (fx.idle >= FX_IDLE) {
            fx_clear();
            for (i = 0; i < n; i++)
                out[2u * i] = 0;
            return;
        }
    } else
        fx.idle = 0;
    gn = fx.gainm ? fx.gainm : fx.gain;
    lvl = fx.level * fx.lmod >> 15;              /* (11 000 * 38 928 < 2^31) */
    for (i = 0; i < n; i++) {
        int32_t x = out[2u * i], y, v;
        x = x > 30000 ? 30000 : x < -30000 ? -30000 : x;
        y = x - fx.dc1x + fx_pole(fx.dc1y);      /* DC block 1 (the voices have none, but the sub and the FM may) */
        fx.dc1x = x;
        fx.dc1y = y;
        y = y > 30000 ? 30000 : y < -30000 ? -30000 : y;
        v = y * gn >> 8;                    /* (30 000 * 64 304 < 2^31) */
        if (v >= 0) {                            /* the shaper: v - v^3 / 6.75 up to 1.5 (out 1.0) ... */
            v = v > 24576 ? 24576 : v;
            v -= ((v * v >> 14) * v >> 14) * 2427 >> 14;
        } else {                                 /* ... and harder below: v - v^3 / 4.32 down to -1.2 (out -0.8) */
            v = v < -19661 ? -19661 : v;
            v -= ((v * v >> 14) * v >> 14) * 3793 >> 14;
        }
        y = v - fx.dc2x + fx_pole(fx.dc2y);      /* DC block 2: the asymmetry's offset */
        fx.dc2x = v;
        fx.dc2y = y;
        fx.tlp += (y - fx.tlp) * TONE_A >> 14;   /* tone: the dark part and the rest mixed in by FUZZ TONE */
        y = fx.tlp + ((y - fx.tlp) * fx.tone >> 14);
        y = y * lvl >> 14;                       /* Q14 -> the output's Q15 level (BREATH's swell in it) */
        if (fx.cab)
            for (k = 0; k < 3u; k++)
                y = fx_biquad(y, CAB[fx.cab - 1u][k], fx.bq[k]);
        fx.lp1 += (y - fx.lp1) * fx.dark_a >> 14;   /* DARK: two one-poles */
        fx.lp2 += (fx.lp1 - fx.lp2) * fx.dark_a >> 14;
        out[2u * i] = fx.lp2;
    }
}
