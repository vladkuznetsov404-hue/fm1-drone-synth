/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Vlad Kuznetsov */
/* The tape echo (audio ISR), over the whole master: after the cassette, before the volume. It is a tape machine in the
 * Space Echo manner: one record head and up to three playback heads on a loop of tape, the heads D, 2 D and 3 D away
 * from the record head (HEADS picks which play). TIME is not a number of samples but how far the heads sit apart, and
 * it moves like a motor does: the spacing glides to its new value at a limited rate (GLIDE: 0.15 s .. 6 s for the whole
 * range, a table of limits per block), and while it glides every repeat is read faster or slower than it was written,
 * so the pitch of the echoes bends the way a real machine's does when you turn the knob (head 3 three times as far as
 * head 1; a fast glide reads the tape backwards for a moment: the dub "throw", clamped to 3 x the speed). WOBBLE adds wow (~0.5 Hz) and
 * flutter (~7 Hz) to the spacing. Every pass through the loop loses highs (DULL), loses DC, and saturates softly; with
 * REPEATS above ~90 the loop sustains and grows until the saturation holds it (the echo plays itself).
 *
 * The line is 32 768 cells of 16 bits at 11 025 Hz (a quarter of the output rate; four samples averaged on the way in,
 * the loop's one-pole and the linear interpolation of the heads are the rest of the filtering: a dark, lo-fi echo is
 * what a tape is). 64 KB in the pool, 2.97 s: the farthest head at the longest TIME (0.95 s) reads 2.85 s back.
 *
 * Per block (32 samples): TIME's slew and the wobble give D at the block's end; each head's position then moves
 * linearly across the block (a Q16 cell position, advanced per sample by 1/4 cell less k x the change of D). The heads
 * read two cells interpolated; every fourth sample a cell is written: the input (the average of four) plus the heads'
 * sum x REPEATS, through DC, saturation and the one-pole.
 *
 * TAPE DRIVE drives the input into the saturation (and lowers its knee), FREEZE (a request from the pads) stops the
 * recording and feeds the heads back at unity: the loop holds what it has.
 *
 * GRAINS: up to six grains read the same line (a granular delay of the tape): each is a window (a triangle) over a
 * stretch of tape, started half its length behind the record head (SCATTER moves the start further back, up to the line's end), read at a pitch
 * (the set {0, +12, -12, +7, +19} st, more of it with GRAIN PITCH) and possibly backwards (REVERSE); DENSITY is how many
 * overlap. The spawn is per block (rare work: the divides live there), the grain loop is a read and a window per
 * sample per live grain. GRAIN FEED writes the grains' sum back into the line.
 *
 * Silence is free: a block of quiet input on an empty line costs a scan of the block; a line that has been quiet for
 * its whole length (3 s: the farthest head may still have an echo coming) is cleared (the one-pole and the integer
 * rounding would hold a stray bit forever), and so is one with MIX 0 for 3 s. The screen reads D, its speed and the cells themselves (a read of what the ISR writes: a picture of the tape). */
#define DL_N 32768u
#define DL_MASK (DL_N - 1u)
#define DL_Q 72253               /* cells in 0.1 ms, Q16 (1.1025 x 65 536) */
#define DL_NG 6u                 /* grains at most */
#define DL_WOW_INC 1558278u      /* 2^32 x 0.5 Hz x CTL / FS */
#define DL_FLUT_INC 21815000u    /* ~7 Hz */
#define DL_QUIET (4u * DL_N)     /* a whole line's worth of samples (2.97 s) without a sound in or out: nothing is left in it */
#define DL_OFF 132300u           /* 3 s */

static int16_t dl_cells[DL_N] __attribute__((section(".pool")));

static const int16_t DL_HG[4] = {0, 32767, 20316, 15073};   /* the heads' weight by how many play: 1, 0.62, 0.46 */
static const int16_t DL_FRZ[4] = {0, 32767, 26420, 23745};  /* the loop at unity with 1, 2, 3 heads (1 / (n x weight)) */
/* GLIDE: the most D changes per block (Q16 cells) so that TIME's whole range (8 900 x 0.1 ms) takes 0.15 x 40^(g/100)
 * seconds, in steps of ten (1 378 blocks a second). */
static const int32_t DL_GLIDE[11] = {3110000, 2150000, 1487000, 1029000, 711000, 492000, 340000, 235000, 162600, 112500, 77800};
static const int32_t GR_STEP[5] = {16384, 32768, 8192, 24550, 49093};   /* a grain's read speed: 0, +12, -12, +7, +19 st */

static struct {
    uint32_t w, sub;             /* the tape: cells written, samples into the next one (0..3) */
    int32_t acc;                 /* the input of the cell being made */
    int32_t d, vel, dprev;       /* D (Q16 cells), its change per block, D with the wobble at the last block's end */
    int32_t lp, dc;              /* the loop's one-pole, and the DC block's mean (Q8: a coarser one stalls a few hundred LSB off) */
    uint32_t wow, flut, quiet, off_n;
    uint8_t dirty;               /* the line holds something (0: all zero) */
    volatile uint8_t freeze;     /* pads -> ISR: the loop holds (no recording, unity feedback) */
    volatile int32_t d_pub, v_pub;   /* ISR -> screen: D (Q16 cells) and its change per block */
    volatile uint32_t w_pub;
    uint32_t gmask, grnd;        /* the live grains (a bit each), the spawn's xorshift */
    int32_t gnext;               /* samples to the next grain */
    volatile int16_t gp_gap[DL_NG], gp_w[DL_NG];   /* ISR -> screen: each live grain's distance behind the record head (cells) and window (0..255) */
} dl = {.grnd = 0x2545F491u};

typedef struct { uint32_t pos, ph, inc, rem; int32_t step, amp; } gr_t;
static gr_t gr[DL_NG];

static __attribute__((noinline)) void dly_clear(void)
{
    memset(dl_cells, 0, sizeof dl_cells);
    dl.lp = dl.dc = dl.acc = 0;
    dl.quiet = 0;
    dl.dirty = 0;
    dl.gmask = 0;
    dl.gnext = 0;
}

static __attribute__((noinline)) void dly_motor(void)    /* TIME's slew (per block): the tape speeds up and slows down */
{
    int32_t dt = dly_ms10(set.g[G_DT]) * DL_Q, vt, g = set.g[G_DGLIDE], i = g / 10, f = g % 10, vmax;
    if (!dl.d) {                                 /* the first block: no glide from nothing */
        dl.d = dt;
        dl.dprev = dt;
    }
    i = i > 9 ? 9 : i;
    vmax = DL_GLIDE[i] + (DL_GLIDE[i + 1] - DL_GLIDE[i]) * (g == 100 ? 10 : f) / 10;
    vt = (dt - dl.d) >> (5 + g / 25);            /* (the gain: the farther away, the faster; the limit is the table's) */
    vt = vt > vmax ? vmax : vt < -vmax ? -vmax : vt;
    dl.vel += (vt - dl.vel) >> 4;                /* the motor's inertia */
    dl.d += dl.vel;
    dl.d_pub = dl.d;
    dl.v_pub = dl.vel;
}

static uint32_t grain_rnd(void)
{
    if (!dl.grnd)
        dl.grnd = 0x2545F491u;                   /* (a zeroed state is valid: xorshift needs a seed) */
    dl.grnd ^= dl.grnd << 13;
    dl.grnd ^= dl.grnd >> 17;
    dl.grnd ^= dl.grnd << 5;
    return dl.grnd;
}

static __attribute__((noinline)) void grain_spawn(uint32_t t0)   /* t0: the record head's time (Q16 cells) */
{
    uint32_t sz = (uint32_t)set.g[G_GSIZE], dens = (uint32_t)set.g[G_GDENS], scat = (uint32_t)set.g[G_GSCAT];
    uint32_t r = grain_rnd(), L = (20u + sz * 6u) * 441u / 10u, j, nset, frac, span;
    int32_t step, delta, lo, hi, iv;
    iv = (int32_t)(L * 100u / (100u + dens * 5u));       /* the interval: SIZE / overlap, jittered by SCATTER */
    iv = iv * (int32_t)(100u - scat / 2u + (r >> 8) % (scat + 1u)) / 100;
    dl.gnext += iv < 64 ? 64 : iv;
    for (j = 0; j < DL_NG && ((dl.gmask >> j) & 1u); j++)
        ;
    if (j == DL_NG)
        return;
    r = grain_rnd();
    nset = 1u + (uint32_t)set.g[G_GPITCH] * 4u / 100u;
    step = GR_STEP[(r >> 16) % nset];
    if ((grain_rnd() >> 8) % 100u < (uint32_t)set.g[G_GREV])
        step = -step;
    delta = (int32_t)(((16384 - step) * (int32_t)L) >> 16);   /* how far the grain's gap to the record head changes over its life */
    lo = 64 + (int32_t)(L >> 3) - (delta < 0 ? delta : 0);   /* (SCATTER 0: half a grain length behind: SIZE is the lag too) */
    hi = 32000 - (delta > 0 ? delta : 0);
    hi = hi < lo ? lo : hi;
    span = (uint32_t)(hi - lo);
    frac = (r & 0xFFFFu) * scat / 100u;                  /* SCATTER 0: always the nearest start */
    gr[j].pos = t0 - (((uint32_t)lo + (span * frac >> 16)) << 16);
    gr[j].step = step;
    gr[j].ph = 0;
    gr[j].inc = 0xFFFFFFFFu / L;
    gr[j].rem = L;
    gr[j].amp = 1300000 / (50 + (int32_t)dens);
    dl.gmask |= 1u << j;
}

static void dly_process(int32_t *out, uint32_t n)
{
    int32_t mix = set.g[G_DMIX], gmix = set.g[G_GMIX], wob = set.g[G_DWOB], mixq, gmq, fbq, gfq, hg, a_lp, da, db, dd, mod, big = 0;
    int32_t gin, knee, frz = dl.freeze;
    uint32_t mask = (uint32_t)set.g[G_DHEADS], act[3], nh = 0, pos[3], stp[3], t0, i, j, gm;
    dly_motor();
    dl.w_pub = dl.w;
    if (!mix && !gmix) {
        dl.off_n += n;
        if (dl.off_n > DL_OFF) {                 /* long off: forget the tape, cost nothing */
            if (dl.dirty)
                dly_clear();
            return;
        }
    } else
        dl.off_n = 0;
    for (j = 0; j < 3u; j++)
        if ((mask >> j) & 1u)
            act[nh++] = j + 1u;
    if (!nh)
        return;
    for (i = 0; i < n; i++) {                    /* (an empty line and a quiet input: nothing to do) */
        int32_t x = out[2u * i];
        big |= x > 8 || x < -8;
    }
    if (!big && !dl.dirty) {
        dl.w += n / 4u;                          /* (n is a multiple of 4: the sub-cell position stays) */
        dl.dprev = dl.d;
        return;
    }
    t0 = (dl.w << 16) + (dl.sub << 14);          /* the tape's time in cells, Q16 (wraps with the line: 2^31 | 2^32) */
    if (gmix) {
        dl.gnext -= (int32_t)n;
        if (dl.gnext <= 0)
            grain_spawn(t0);
    }
    dl.wow += DL_WOW_INC;
    dl.flut += DL_FLUT_INC;
    mod = (br_sine(dl.wow) * wob * 23 >> 6) + (br_sine(dl.flut) * wob * 31 >> 12);   /* WOBBLE 100: ~18 cells, 0.4 */
    da = dl.dprev;
    db = dl.d + mod;
    dd = (db - da) >> 5;                         /* D's change per sample */
    dl.dprev = db;
    for (j = 0; j < nh; j++) {
        int32_t s = 16384 - (int32_t)act[j] * dd;
        pos[j] = t0 - act[j] * (uint32_t)da;
        stp[j] = (uint32_t)(s > 49152 ? 49152 : s < -49152 ? -49152 : s);   /* (a fast glide: backwards, at 3 x at most) */
    }
    mixq = mix * 327;
    gmq = gmix * 327;
    gfq = set.g[G_GFB] * 300;                    /* GRAIN FEED 100: 0.92 */
    fbq = frz ? DL_FRZ[nh] : set.g[G_DFB] * 351; /* REPEATS 100: 1.07 */
    gin = 256 + set.g[G_DDRV] * 4;
    knee = 20000 - set.g[G_DDRV] * 140;
    gm = dl.gmask;
    hg = DL_HG[nh];
    a_lp = 30800 - set.g[G_DDULL] * 200;         /* the loop's one-pole at the cell rate: ~5 kHz .. ~0.7 kHz */
    big = 0;
    for (i = 0; i < n; i++) {
        int32_t x = out[2u * i], hs = 0, e, gs = 0;
        for (j = 0; gm && j < DL_NG; j++) {
            gr_t *g;
            uint32_t p, ix, u, w;
            int32_t a, b, v;
            if (!((gm >> j) & 1u))
                continue;
            g = &gr[j];
            p = g->pos;
            ix = (p >> 16) & DL_MASK;
            a = dl_cells[ix];
            b = dl_cells[(ix + 1u) & DL_MASK];
            v = a + ((b - a) * (int32_t)((p >> 8) & 255u) >> 8);
            u = g->ph >> 16;
            w = (u < 32768u ? u : 65535u - u) << 1;          /* the triangle, 0..65 534 */
            gs += (v * (int32_t)w >> 16) * g->amp >> 15;
            g->pos = p + (uint32_t)g->step;
            g->ph += g->inc;
            if (!--g->rem)
                gm &= ~(1u << j);
        }
        for (j = 0; j < nh; j++) {
            uint32_t p = pos[j], ix = (p >> 16) & DL_MASK;
            int32_t a = dl_cells[ix], b = dl_cells[(ix + 1u) & DL_MASK];
            hs += a + ((b - a) * (int32_t)((p >> 8) & 255u) >> 8);
            pos[j] = p + stp[j];
        }
        e = hs * hg >> 15;
        out[2u * i] = x + (e * mixq >> 15) + (gs * gmq >> 15);
        dl.acc += x;
        big |= (x > 8 || x < -8 || e > 8 || e < -8);
        if (++dl.sub == 4u) {                    /* a cell: the average of four (driven), the repeats and the grains fed back */
            int32_t v = (frz ? 0 : (dl.acc >> 2) * gin >> 8) + (e * fbq >> 15) + (gs * gfq >> 15), a;
            dl.dc += (v * 256 - dl.dc) >> 8;
            v -= dl.dc >> 8;
            a = v < 0 ? -v : v;
            if (a > knee)
                a = knee + ((a - knee) >> 2);    /* the tape saturates */
            a = a > 31000 ? 31000 : a;
            v = v < 0 ? -a : a;
            dl.lp += (v - dl.lp) * a_lp >> 15;
            dl_cells[dl.w & DL_MASK] = (int16_t)dl.lp;
            dl.w++;
            dl.sub = 0;
            dl.acc = 0;
        }
    }
    dl.gmask = gm;
    if (dl.gmask) {                              /* the grains for the screen */
        uint32_t te = (dl.w << 16) + (dl.sub << 14);
        for (j = 0; j < DL_NG; j++) {
            uint32_t u = gr[j].ph >> 16;
            dl.gp_gap[j] = ((dl.gmask >> j) & 1u) ? (int16_t)(((te - gr[j].pos) >> 16) & DL_MASK) : 0;
            dl.gp_w[j] = ((dl.gmask >> j) & 1u) ? (int16_t)(((u < 32768u ? u : 65535u - u) >> 7) + 1u) * (gr[j].step < 0 ? -1 : 1) : 0;   /* (+ 1: a grain just born is live; < 0: backwards) */
        }
    } else
        for (j = 0; j < DL_NG; j++)
            dl.gp_w[j] = 0;
    if (big) {
        dl.dirty = 1;
        dl.quiet = 0;
    } else if ((dl.quiet += n) >= DL_QUIET && dl.dirty)
        dly_clear();
}
