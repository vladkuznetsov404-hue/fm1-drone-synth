/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Vlad Kuznetsov */
/* SHOEGAZE's two last stages (audio ISR), after the fuzz, the cabinet and DARK: a chorus and a reverb. Only in that
 * mode (G_MODE): in HARSH nothing here runs and the state is dropped.
 *
 * The chorus: a 2 048-sample line, two taps swung by slow sines (0.55 and 0.83 Hz) around 16 and 20 ms, the depth
 * growing with CHORUS (up to +-7 ms); the output is the dry and the taps' mean crossfaded by up to one half, so the
 * level stays. Linear interpolation (Q8 delay), integer throughout.
 *
 * The reverb (Freeverb in little): four parallel combs (1 116, 1 188, 1 277, 1 356 samples, a one-pole in each feedback
 * loop to take the highs off the tail) into two all-passes (556, 441), 12 KB of int16 in the pool. REVERB sets the
 * wet level and the feedback (0.75 .. 0.93: a tail of ~1 .. 4 s). A tail has to outlive the voices (fx.c clears its
 * own states after 0.19 s of silence), so the reverb keeps its own account: it runs while the input or its own
 * output is above a few LSB, and is cleared (the integer loops would hang at +-1 for ever) once both have been quiet
 * for 0.5 s. A sound that is not there costs a scan of the block. */
#define CH_N 2048u
#define RV_QUIET 22050u
#define RV_NC 4u
static const uint16_t RV_LEN[RV_NC] = {1116, 1188, 1277, 1356};
#define RV_AP0 556u
#define RV_AP1 441u
#define RV_TOTAL (1116u + 1188u + 1277u + 1356u + RV_AP0 + RV_AP1)

static int16_t ch_buf[CH_N] __attribute__((section(".pool")));
static int16_t rv_buf[RV_TOTAL] __attribute__((section(".pool")));

static struct {
    uint32_t cw;                 /* the chorus' write position */
    uint32_t ph1, ph2;           /* the taps' LFOs */
    uint32_t ci[RV_NC], ai[2];   /* the combs' and the all-passes' positions */
    int32_t cl[RV_NC];           /* the combs' one-poles */
    uint32_t quiet;              /* samples both the reverb's input and output were quiet */
    uint8_t live;                /* the reverb holds a sound */
    uint8_t on;                  /* the stages have run (their state is not zero) */
} sh;

static __attribute__((noinline)) void shoe_clear(void)
{
    memset(ch_buf, 0, sizeof ch_buf);
    memset(rv_buf, 0, sizeof rv_buf);
    memset(&sh, 0, sizeof sh);
}

static void shoe_process(int32_t *out, uint32_t n)
{
    int32_t chorus, rev, depth, cm, fb, rm, pk = 0, ipk = 0;
    uint32_t i, k, off;
    if (fx.bypass)
        return;
    if (set.g[G_MODE] != MODE_SHOE) {
        if (sh.on)
            shoe_clear();
        return;
    }
    chorus = cur.p[P_CHORUS];
    rev = cur.p[P_REVERB];
    if (chorus) {
        depth = 20 + chorus * 3;                 /* samples: +-0.5 .. +-7 ms */
        cm = chorus * 164;                       /* the mix: 0 .. one half, Q15 */
        sh.on = 1;
        for (i = 0; i < n; i++) {
            int32_t x = out[2u * i], d1, d2, a, b, t1, t2, w;
            uint32_t ix;
            ch_buf[sh.cw & (CH_N - 1u)] = (int16_t)(x > 32767 ? 32767 : x < -32768 ? -32768 : x);
            sh.ph1 += 53557u;
            sh.ph2 += 80836u;
            d1 = (704 << 8) + (br_sine(sh.ph1) * depth >> 7);         /* ~16 ms */
            d2 = (880 << 8) + (br_sine(sh.ph2) * depth >> 7);         /* ~20 ms */
            ix = (sh.cw - (uint32_t)(d1 >> 8)) & (CH_N - 1u);
            a = ch_buf[ix];
            b = ch_buf[(ix - 1u) & (CH_N - 1u)];
            t1 = a + ((b - a) * (d1 & 255) >> 8);
            ix = (sh.cw - (uint32_t)(d2 >> 8)) & (CH_N - 1u);
            a = ch_buf[ix];
            b = ch_buf[(ix - 1u) & (CH_N - 1u)];
            t2 = a + ((b - a) * (d2 & 255) >> 8);
            w = (t1 + t2) >> 1;
            out[2u * i] = x + ((w - x) * cm >> 15);
            sh.cw++;
        }
    }
    for (i = 0; i < n; i++) {
        int32_t x = out[2u * i];
        ipk |= x > 4 || x < -4;
    }
    if (!rev) {
        if (sh.live) {                           /* REVERB 0: the tail is cut */
            memset(rv_buf, 0, sizeof rv_buf);
            memset(sh.cl, 0, sizeof sh.cl);
            sh.live = 0;
        }
        return;
    }
    if (!ipk && !sh.live)
        return;
    sh.live = 1;
    sh.on = 1;
    fb = 24576 + rev * 60;                       /* 0.75 .. 0.93 */
    rm = rev * 180;                              /* the wet level: 0 .. 0.55 */
    for (i = 0; i < n; i++) {
        int32_t x = out[2u * i], inp = x >> 2, s = 0, y, bo;
        off = 0;
        for (k = 0; k < RV_NC; k++) {
            uint32_t ix = off + sh.ci[k];
            int32_t c = rv_buf[ix], v;
            sh.cl[k] += (c - sh.cl[k]) * 13000 >> 15;                 /* the damping: ~1.8 kHz */
            v = inp + (sh.cl[k] * fb >> 15);
            rv_buf[ix] = (int16_t)(v > 32767 ? 32767 : v < -32768 ? -32768 : v);
            if (++sh.ci[k] >= RV_LEN[k])
                sh.ci[k] = 0;
            s += c;
            off += RV_LEN[k];
        }
        bo = rv_buf[off + sh.ai[0]];                                  /* the all-passes, g = 1/2 */
        y = bo - s;
        rv_buf[off + sh.ai[0]] = (int16_t)(s + (bo >> 1) > 32767 ? 32767 : s + (bo >> 1) < -32768 ? -32768 : s + (bo >> 1));
        if (++sh.ai[0] >= RV_AP0)
            sh.ai[0] = 0;
        off += RV_AP0;
        bo = rv_buf[off + sh.ai[1]];
        s = y;
        y = bo - s;
        rv_buf[off + sh.ai[1]] = (int16_t)(s + (bo >> 1) > 32767 ? 32767 : s + (bo >> 1) < -32768 ? -32768 : s + (bo >> 1));
        if (++sh.ai[1] >= RV_AP1)
            sh.ai[1] = 0;
        y = y * rm >> 15;
        pk |= y > 6 || y < -6;
        out[2u * i] = x + y;
    }
    if (ipk || pk)
        sh.quiet = 0;
    else if ((sh.quiet += n) >= RV_QUIET) {
        memset(rv_buf, 0, sizeof rv_buf);
        memset(sh.cl, 0, sizeof sh.cl);
        memset(sh.ci, 0, sizeof sh.ci);
        memset(sh.ai, 0, sizeof sh.ai);
        sh.live = 0;
        sh.quiet = 0;
    }
}

/* ---- SPIN: the rotor (audio ISR, both bodies) -----------------------------------------------------------------------
 * The sound turns between the two speakers like a Leslie's horn: a pan that swings from one side to the other, and a
 * dip of the level twice a turn (the horn facing away and the other way round), so a single speaker hears it too. SPIN
 * is the rotor's speed: 0 = still (the stage is skipped, the two channels are the same sound), 1 = a slow breath of
 * 0.12 Hz, 100 = a whirl of 12 Hz, quadratic in between (most of the knob is slow). The rotor has weight: the speed it
 * is going at follows the knob over ~0.7 s, so a turn is an acceleration or a slowing down, the "вжух". The depth
 * comes in over the first fifth of the knob. The gains are worked out once per block and slide between blocks.
 * LFO target SPIN adds up to +-100 to the knob. spin_pub is the rotor's phase for the picture. */
#define SP_INC 31165u            /* 0.01 Hz as a per-block phase step: 2^32 * 0.01 * CTL / FS */

static struct {
    uint32_t ph, inc;            /* the rotor's phase, its speed now (per block) */
    int32_t gl, gr;              /* the channels' gains now, Q20 */
    int32_t dl, dr;              /* ... and the steps per sample */
    uint8_t on;
} sp;
static volatile uint16_t spin_pub;   /* the rotor's phase (16 bits) and ... */
static volatile uint16_t spin_hz100; /* ... its speed in 0.01 Hz, for the picture */

static void spin_block(uint32_t n)
{
    int32_t s = cur.p[P_SPIN] + lf.spin + (cur.p[P_SPIN] > 0 ? GDR(gdr.spin) : 0), a, u, v, gl, gr, tr, tgt;
    s = s < 0 ? 0 : s > 100 ? 100 : s;
    tgt = s ? (int32_t)(12 + 1188 * s * s / 10000) : 0;           /* 0.01 Hz */
    {
        int32_t want = tgt * (int32_t)SP_INC, d = want - (int32_t)sp.inc;
        sp.inc = (uint32_t)((d > -512 && d < 512) ? want : (int32_t)sp.inc + (d >> 9));
    }
    if (!s && !sp.inc) {
        sp.on = 0;
        sp.gl = sp.gr = 1 << 20;
        sp.dl = sp.dr = 0;
        spin_hz100 = 0;
        return;
    }
    sp.on = 1;
    sp.ph += sp.inc;
    spin_pub = (uint16_t)(sp.ph >> 16);
    spin_hz100 = (uint16_t)(sp.inc / SP_INC);
    a = (s > 20 ? 20 : s) * 16384 / 20;            /* the depth, Q14 (so a x (32 768 + u) stays below 2^31) */
    u = br_sine(sp.ph);
    v = br_sine(sp.ph << 1);
    gl = 32768 - ((a * (32768 - u) >> 14) * 11469 >> 15);          /* pan: 1 - 0.35 a (1 - u) */
    gr = 32768 - ((a * (32768 + u) >> 14) * 11469 >> 15);
    tr = 32768 - ((a * (32768 - v) >> 14) * 4096 >> 15);           /* the dip: 1 - 0.125 a (1 - v) */
    gl = gl * tr >> 15;
    gr = gr * tr >> 15;
    sp.dl = ((gl << 5) - sp.gl) / (int32_t)n;
    sp.dr = ((gr << 5) - sp.gr) / (int32_t)n;
}
