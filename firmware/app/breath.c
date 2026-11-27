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
    br.pm = (br_sine(br.ph[0]) * br.a_pitch >> 15) << 4;  /* (32 767 * 4 845 < 2^31) */
    idx = cur.p[P_DARK] + ((br_sine(br.ph[1]) * br.a_cut >> 15) >> 8);   /* (32 767 * 6 400 < 2^31) */
    fx.dark_a = DARK_A[idx < 0 ? 0 : idx > 100 ? 100 : idx];
    fx.lmod = 32768 + (br_sine(br.ph[2]) * br.a_lvl >> 15);
}
