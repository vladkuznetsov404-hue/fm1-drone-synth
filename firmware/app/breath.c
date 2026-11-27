/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Vlad Kuznetsov */
/* BREATH (audio ISR), milestone 4 of docs/SPEC.md: three slow LFOs at unrelated rates (RATE, x 1.375, x 0.75: they
 * line up again only every ~11 of the slowest's cycles), one each on the pitch, the DARK cutoff and the level, so the
 * drone moves without a repeating pattern. BREATH is the depth of all three (and nudges the rate up to +20 %); TO
 * PITCH, TO CUTOFF and TO LEVEL weigh them. At BREATH and every TO at 100: +-8 cents, +-25 DARK steps (a quarter of
 * its range), +-1.5 dB.
 *
 * Once per block (32 samples, ~1.4 kHz): far faster than the LFOs (1 Hz at most), so the steps are inaudible and
 * the sample loops stay as they were. The weights are worked out by breath_update (the divides) only when a value
 * changed. Results: br.pm (a ratio offset for the voices' increments, Q24), fx.dark_a (the cutoff with the LFO),
 * fx.lmod (the level's factor, Q15). */
#define BR_CENTS 77528           /* 8 cents as a ratio offset, Q24 (8 * ln 2 / 1200 * 2^24) */
#define BR_CUT 25                /* DARK steps at full depth */
#define BR_LVL 6160              /* +-1.5 dB, Q15 (10^(1.5/20) - 1 = 0.188) */
#define BR_INC 31167u            /* RATE 1 (0.01 Hz) as a per-block phase step: 2^32 * 0.01 * CTL / FS */

/* The LFO proper (the LFO page): one wave at RATE, in a SHAPE, that moves one TARGET by DEPTH. It runs beside BREATH's
 * three (which keep drifting the pitch, the cutoff and the level as before) and adds to them: at DEPTH 100 it is
 * +-30 cents, +-50 DARK steps, +-50 % of the level, +-40 % of the fuzz's gain, or +-100 of SPIN. RANDOM glides between
 * two values drawn at each cycle; SQUARE is smoothed over ~6 ms so it does not click. */
#define LF_CENTS 2907            /* 30 cents per 100 DEPTH, a ratio offset in Q24 (30 * 9 691 / 100) */

static struct {
    uint32_t ph, rng;
    int32_t a, b, sm;            /* RANDOM's two values; the smoothed SQUARE */
    int32_t v;                   /* the wave now, +-32 767 (the picture shows it: lf_pub) */
    int32_t pm, cut, lvl, gmod, spin;   /* what it moves now: Q24 ratio offset, DARK steps, Q15, Q15, SPIN steps */
} lf = {.rng = 0x2545F491u};
static volatile int16_t lf_pub;  /* the wave, published for the picture */
static volatile uint16_t lf_ph_pub;   /* ... and its phase (16 bits) */

static __attribute__((noinline)) void lf_wrap(void)   /* RANDOM: a new value at each cycle */
{
    lf.rng ^= lf.rng << 13;
    lf.rng ^= lf.rng >> 17;
    lf.rng ^= lf.rng << 5;
    lf.a = lf.b;
    lf.b = (int32_t)(lf.rng >> 16) - 32768;
}

enum { BV_BREATH, BV_RATE, BV_PITCH, BV_CUT, BV_LVL, BV_N };

static struct {
    int16_t last[BV_N];          /* the values breath_update last used (-1: never) */
    uint32_t ph[3], inc;         /* the LFOs' phases; the slowest's step per block */
    int32_t a_pitch, a_cut, a_lvl;   /* the depths: Q24 >> 4, DARK steps Q8, Q15 */
    int32_t pm;                  /* -> the voices: the pitch's ratio offset now, Q24 */
} br = {.last = {-1, -1, -1, -1, -1}};

static __attribute__((noinline)) void breath_update(void)
{
    int32_t d = cur.p[P_BREATH];
    br.last[BV_BREATH] = cur.p[P_BREATH];
    br.last[BV_RATE] = cur.p[P_RATE];
    br.last[BV_PITCH] = cur.p[P_TOPITCH];
    br.last[BV_CUT] = cur.p[P_TOCUT];
    br.last[BV_LVL] = cur.p[P_TOLEVEL];
    br.inc = (uint32_t)cur.p[P_RATE] * BR_INC * (uint32_t)(500 + d) / 500u;   /* (100 * 31 167 * 600 < 2^31) */
    br.a_pitch = (BR_CENTS >> 4) * cur.p[P_TOPITCH] / 100 * d / 100;
    br.a_cut = BR_CUT * 256 * cur.p[P_TOCUT] / 100 * d / 100;
    br.a_lvl = BR_LVL * cur.p[P_TOLEVEL] / 100 * d / 100;
}

static int32_t br_sine(uint32_t ph)   /* the parabolic sine of the sub: 4 u (1 - |u|), Q15 */
{
    int32_t u = (int16_t)(ph >> 16), y = u * (32768 - (u < 0 ? -u : u)) >> 13;
    return y > 32767 ? 32767 : y;
}

static void lfo_block(void)      /* once per block: the LFO's wave and what it moves */
{
    int32_t t, v, amt = cur.p[P_LAMT], q, d;
    uint32_t inc = (uint32_t)cur.p[P_RATE] * BR_INC;
    lf.ph += inc;
    if (lf.ph < inc)
        lf_wrap();
    t = (int32_t)(lf.ph >> 16);
    switch (cur.p[P_LSHAPE]) {
    case 1: v = (t < 32768 ? t : 65535 - t) * 2 - 32768; break;       /* triangle */
    case 2: v = t - 32768; break;                                      /* ramp up */
    case 3: lf.sm += ((t < 32768 ? 32767 : -32767) - lf.sm) >> 3; v = lf.sm; break;   /* square, smoothed */
    case 4: v = lf.a + (((lf.b - lf.a) >> 1) * t >> 15); break;       /* random, gliding */
    default: v = br_sine(lf.ph); break;
    }
    lf.v = v;
    lf_pub = (int16_t)(v > 32767 ? 32767 : v < -32767 ? -32767 : v);
    lf_ph_pub = (uint16_t)(lf.ph >> 16);
    q = v >> 5;                                  /* +-1023 */
    d = cur.p[P_LDEST];                          /* only the target moves */
    lf.pm = d == 0 ? (q * (amt * LF_CENTS)) >> 10 : 0;
    lf.cut = d == 1 ? (q * amt) >> 11 : 0;
    lf.lvl = d == 2 ? ((v >> 1) * amt * 41) >> 12 : 0;
    lf.gmod = d == 3 ? ((v >> 1) * amt * 33) >> 12 : 0;
    lf.spin = d == 4 ? (q * amt) >> 10 : 0;
}

static void breath_block(void)   /* once per block, before the voices and the chain */
{
    int32_t idx;
    if (br.last[BV_BREATH] != cur.p[P_BREATH] || br.last[BV_RATE] != cur.p[P_RATE] ||
        br.last[BV_PITCH] != cur.p[P_TOPITCH] || br.last[BV_CUT] != cur.p[P_TOCUT] ||
        br.last[BV_LVL] != cur.p[P_TOLEVEL])
        breath_update();
    br.ph[0] += br.inc;
    br.ph[1] += br.inc + (br.inc >> 1) - (br.inc >> 3);   /* x 1.375 */
    br.ph[2] += br.inc - (br.inc >> 2);                   /* x 0.75 */
    lfo_block();
    br.pm = (br_sine(br.ph[0]) * br.a_pitch >> 15) * 16 + lf.pm;  /* (32 767 * 4 845 < 2^31) */
    idx = cur.p[P_DARK] + ((br_sine(br.ph[1]) * br.a_cut >> 15) >> 8) + lf.cut;   /* (32 767 * 6 400 < 2^31) */
    fx.dark_a = DARK_A[idx < 0 ? 0 : idx > 100 ? 100 : idx];
    fx.lmod = 32768 + (br_sine(br.ph[2]) * br.a_lvl >> 15) + lf.lvl;
    fx.lmod = fx.lmod < 0 ? 0 : fx.lmod;
    idx = (fx.gain >> 3) * (32768 + lf.gmod) >> 12;   /* the fuzz's gain with the LFO (Q8; the shaper's input stays < 2^31) */
    fx.gainm = idx > 70000 ? 70000 : idx < 0 ? 0 : idx;
}
