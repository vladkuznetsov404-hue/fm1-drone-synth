/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Vlad Kuznetsov */
/* The tape echo (audio ISR), over the whole master: after the cassette, before the volume. It is a tape machine in the
 * Space Echo manner: one record head and up to three playback heads on a loop of tape, the heads D, 2 D and 3 D away
 * from the record head (HEADS picks which play). TIME is not a number of samples but how far the heads sit apart, and
 * it moves like a motor does: the spacing glides to its new value at a limited rate (~6 s for the whole range), and
 * while it glides every repeat is read faster or slower than it was written, so the pitch of the echoes bends the way a
 * real machine's does when you turn the knob (head 3 three times as far as head 1). WOBBLE adds wow (~0.5 Hz) and
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
 * Silence is free: a block of quiet input on an empty line costs a scan of the block; a line that has been quiet for
 * its whole length (3 s: the farthest head may still have an echo coming) is cleared (the one-pole and the integer
 * rounding would hold a stray bit forever), and so is one with MIX 0 for 3 s. The screen reads D, its speed and the cells themselves (a read of what the ISR writes: a picture of the tape). */
#define DL_N 32768u
#define DL_MASK (DL_N - 1u)
#define DL_Q 72253               /* cells in 0.1 ms, Q16 (1.1025 x 65 536) */
#define DL_VMAX 83886            /* the most D changes per block, Q16 cells: 0.04 cell a sample, head 1 +-16 % pitch */
#define DL_WOW_INC 1558278u      /* 2^32 x 0.5 Hz x CTL / FS */
#define DL_FLUT_INC 21815000u    /* ~7 Hz */
#define DL_QUIET (4u * DL_N)     /* a whole line's worth of samples (2.97 s) without a sound in or out: nothing is left in it */
#define DL_OFF 132300u           /* 3 s */

static int16_t dl_cells[DL_N] __attribute__((section(".pool")));

static const int16_t DL_HG[4] = {0, 32767, 20316, 15073};   /* the heads' weight by how many play: 1, 0.62, 0.46 */

static struct {
    uint32_t w, sub;             /* the tape: cells written, samples into the next one (0..3) */
    int32_t acc;                 /* the input of the cell being made */
    int32_t d, vel, dprev;       /* D (Q16 cells), its change per block, D with the wobble at the last block's end */
    int32_t lp, dc;              /* the loop's one-pole, and the DC block's mean (Q8: a coarser one stalls a few hundred LSB off) */
    uint32_t wow, flut, quiet, off_n;
    uint8_t dirty;               /* the line holds something (0: all zero) */
    volatile int32_t d_pub, v_pub;   /* ISR -> screen: D (Q16 cells) and its change per block */
    volatile uint32_t w_pub;
} dl;

static __attribute__((noinline)) void dly_clear(void)
{
    memset(dl_cells, 0, sizeof dl_cells);
    dl.lp = dl.dc = dl.acc = 0;
    dl.quiet = 0;
    dl.dirty = 0;
}

static __attribute__((noinline)) void dly_motor(void)    /* TIME's slew (per block): the tape speeds up and slows down */
{
    int32_t dt = dly_ms10(set.g[G_DT]) * DL_Q, vt;
    if (!dl.d) {                                 /* the first block: no glide from nothing */
        dl.d = dt;
        dl.dprev = dt;
    }
    vt = (dt - dl.d) >> 9;
    vt = vt > DL_VMAX ? DL_VMAX : vt < -DL_VMAX ? -DL_VMAX : vt;
    dl.vel += (vt - dl.vel) >> 4;                /* the motor's inertia */
    dl.d += dl.vel;
    dl.d_pub = dl.d;
    dl.v_pub = dl.vel;
}

static void dly_process(int32_t *out, uint32_t n)
{
    int32_t mix = set.g[G_DMIX], wob = set.g[G_DWOB], mixq, fbq, hg, a_lp, da, db, dd, mod, big = 0;
    uint32_t mask = (uint32_t)set.g[G_DHEADS], act[3], nh = 0, pos[3], stp[3], t0, i, j;
    dly_motor();
    dl.w_pub = dl.w;
    if (!mix) {
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
    dl.wow += DL_WOW_INC;
    dl.flut += DL_FLUT_INC;
    mod = (br_sine(dl.wow) * wob * 23 >> 6) + (br_sine(dl.flut) * wob * 31 >> 12);   /* WOBBLE 100: ~18 cells, 0.4 */
    da = dl.dprev;
    db = dl.d + mod;
    dd = (db - da) >> 5;                         /* D's change per sample */
    dl.dprev = db;
    t0 = (dl.w << 16) + (dl.sub << 14);          /* the tape's time in cells, Q16 (wraps with the line: 2^31 | 2^32) */
    for (j = 0; j < nh; j++) {
        pos[j] = t0 - act[j] * (uint32_t)da;
        stp[j] = 16384u - (uint32_t)((int32_t)act[j] * dd);
    }
    mixq = mix * 327;
    fbq = set.g[G_DFB] * 351;                    /* REPEATS 100: 1.07 */
    hg = DL_HG[nh];
    a_lp = 30800 - set.g[G_DDULL] * 200;         /* the loop's one-pole at the cell rate: ~5 kHz .. ~0.7 kHz */
    big = 0;
    for (i = 0; i < n; i++) {
        int32_t x = out[2u * i], hs = 0, e;
        for (j = 0; j < nh; j++) {
            uint32_t p = pos[j], ix = (p >> 16) & DL_MASK;
            int32_t a = dl_cells[ix], b = dl_cells[(ix + 1u) & DL_MASK];
            hs += a + ((b - a) * (int32_t)((p >> 8) & 255u) >> 8);
            pos[j] = p + stp[j];
        }
        e = hs * hg >> 15;
        out[2u * i] = x + (e * mixq >> 15);
        dl.acc += x;
        big |= (x > 8 || x < -8 || e > 8 || e < -8);
        if (++dl.sub == 4u) {                    /* a cell: the average of four, and the repeats fed back */
            int32_t v = (dl.acc >> 2) + (e * fbq >> 15), a;
            dl.dc += (v * 256 - dl.dc) >> 8;
            v -= dl.dc >> 8;
            a = v < 0 ? -v : v;
            if (a > 20000)
                a = 20000 + ((a - 20000) >> 2);  /* the tape saturates */
            a = a > 31000 ? 31000 : a;
            v = v < 0 ? -a : a;
            dl.lp += (v - dl.lp) * a_lp >> 15;
            dl_cells[dl.w & DL_MASK] = (int16_t)dl.lp;
            dl.w++;
            dl.sub = 0;
            dl.acc = 0;
        }
    }
    if (big) {
        dl.dirty = 1;
        dl.quiet = 0;
    } else if ((dl.quiet += n) >= DL_QUIET && dl.dirty)
        dly_clear();
}
