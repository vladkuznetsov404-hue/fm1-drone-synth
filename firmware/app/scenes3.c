/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Vlad Kuznetsov */
/* The last pictures (main loop), after scenes2.c: ENV, LFO (two pages), EDIT, GLO and the quasar's FX. A page with
 * numbers in rows and a bar under each was the thing to get rid of: here every knob changes something you can see.
 *   ENV     the volume's path in time, a dot running along it: SWELL is the climb, RELEASE the fall, SLIDE the glide
 *           a note makes to the next one of a chord.
 *   LFO     the wave (SHAPE) at its speed (RATE), a dot riding it, and the five things it can move (TARGET) with how
 *           far it moves the chosen one (DEPTH). LFO 2 is BREATH's three slow waves, one per thing they move.
 *   EDIT    SAW: the layers' saws drifting against each other (the beating), the sub under them; FM: the carrier
 *           bent by the modulator.
 *   GLO     the MIDI channel as sixteen lamps, the TRS jack, the volume as a speaker.
 *   corona  the quasar with its effects on it: FUZZ burns the jets, SHIMMER doubles the disc, BLOOM sends rings out,
 *           LEVEL sizes the core, SPIN turns the whole thing.
 * All inside the 240 x 124 canvas; integer maths with the same trig table (cs, sn: 120 steps a turn). */

static int32_t sc_tri(int32_t i)                 /* a triangle of the same 120 steps: -127 .. 127, 0 at the start */
{
    int32_t u = (i % 120 + 120) % 120;
    return u < 30 ? u * 127 / 30 : u < 90 ? (60 - u) * 127 / 30 : (u - 120) * 127 / 30;
}

static int32_t sc_wave(int32_t shape, int32_t i)  /* one cycle = 120 steps: the LFO's shape, -127 .. 127 */
{
    static const int8_t RND[6] = {40, -90, 100, -30, 70, 40};   /* RANDOM: a typical run of values (the real ones are drawn at each cycle) */
    int32_t u = (i % 120 + 120) % 120, k = u / 24, f = u % 24;
    switch (shape) {
    case 1: return sc_tri(i);
    case 2: return u * 254 / 119 - 127;
    case 3: return u < 60 ? 127 : -127;
    case 4: return RND[k] + (RND[k + 1] - RND[k]) * f / 24;
    default: return sn(i);
    }
}

/* ---- ENV ---- */
static void sc_env(uint32_t y)                   /* 240 x 124 */
{
    int32_t att = cur.p[P_ATTACK], rel = cur.p[P_RELEASE], sl = set.g[G_SLIDE];
    int32_t a = 6 + 150 * att / (att + rel + 20), r = 6 + 150 * rel / (att + rel + 20), p = 224 - a - r, x, py = 0, i, cyc, dot, x0 = 8;
    int32_t base = 78, h = 64, pos;
    char b[24];
    p = p < 36 ? 36 : p;
    cv_begin(240, 124, T_BG);
    cv_rect(8, base + 1, 224, 1, T_LINE);
    for (x = 0; x < a + p + r && x < 224; x++) {
        int32_t t, v;
        if (x < a) {                             /* the swell: slow at first, then it climbs (a volume's ear-curve) */
            t = x * 100 / a;
            v = t * t / 100;
        } else if (x < a + p) {
            v = 100;
        } else {
            t = (x - a - p) * 100 / r;
            v = (100 - t) * (100 - t) / 100;
        }
        v = v * h / 100;
        cv_rect(x0 + x, base - v, 1, v, T_TINT);
        if (x)
            cv_line_t(x0 + x - 1, base - py, x0 + x, base - v, T_THEME, 2);
        py = v;
    }
    cv_rect(x0 + a, 8, 1, base - 8, T_DIM);      /* the key's down at the left, up at the end of the plateau */
    cv_rect(x0 + a + p, 8, 1, base - 8, T_DIM);
    cv_text(x0 + 2, 2, &AF_S, "KEY DOWN", T_DIM);
    cv_text(x0 + a + p + 3 + 36 > 232 ? 232 - 36 : x0 + a + p + 3, 2, &AF_S, "KEY UP", T_DIM);
    str_cpy(b, "SWELL ", sizeof b);
    fixed2(b + 6, att, 1);
    str_cat(b, " S", sizeof b);
    cv_text(x0, 85, &AF_S, b, T_MID);
    str_cpy(b, "RELEASE ", sizeof b);
    fixed2(b + 8, rel, 1);
    str_cat(b, " S", sizeof b);
    cv_text_r(232, 85, &AF_S, b, T_MID, T_BG);
    cyc = (int32_t)(fm1_ms / 40u) % 100;         /* the dot runs the path in 4 s */
    pos = cyc * (a + p + r) / 100;
    if (pos < a) {
        int32_t t = pos * 100 / a;
        dot = t * t / 100;
    } else if (pos < a + p) {
        dot = 100;
    } else {
        int32_t t = (pos - a - p) * 100 / r;
        dot = (100 - t) * (100 - t) / 100;
    }
    sc_disc(x0 + pos, base - dot * h / 100, 3, T_ACCENT);
    cv_rect(8, 99, 224, 1, T_LINE);              /* SLIDE: a note and the next one of the chord, the glide between */
    str_cpy(b, "SLIDE ", sizeof b);
    fixed2(b + 6, sl, 1);
    str_cat(b, sl ? " S" : " S: AT ONCE", sizeof b);
    cv_text(8, 106, &AF_S, b, T_MID);
    py = 0;
    for (i = 0; i < 96; i++) {                   /* a step up at 8, then a one-pole towards it: 63 % after one time constant */
        int32_t tc = sl * 96 / 60 + 1, v, t = (i - 8) * 100 / tc;
        if (i < 8)
            v = 0;
        else if (!sl)
            v = 100;
        else
            v = t >= 300 ? 100 : t < 100 ? t * 63 / 100 : t < 200 ? 63 + (t - 100) * 23 / 100 : 86 + (t - 200) * 9 / 100;
        if (i)
            cv_line(x0 + 124 + i - 1, 120 - py * 14 / 100, x0 + 124 + i, 120 - v * 14 / 100, T_ACCENT);
        py = v;
    }
    cv_blit(0, y);
}

/* ---- LFO ---- */
static void sc_lfo(uint32_t y)                   /* 240 x 124 */
{
    static const char *const D[5] = {"PITCH", "CUTOFF", "LEVEL", "FUZZ", "SPIN"};
    int32_t shape = cur.p[P_LSHAPE], dest = cur.p[P_LDEST], amt = cur.p[P_LAMT], rate = cur.p[P_RATE], i, py = 0, py2 = 0;
    int32_t mid = 38, amp = 32, vnow = lf_pub, ph = lf_ph_pub, dx, dy, k, sw;
    char b[24];
    cv_begin(240, 124, T_BG);
    cv_rrect(4, 2, 232, 78, 4, T_SURF, T_BG);
    cv_rect(10, mid, 220, 1, T_LINE);
    for (i = 0; i <= 112; i++) {                 /* the shape at full size, faint; at the DEPTH's size, bright */
        int32_t w = sc_wave(shape, i * 120 / 112), x = 10 + i * 220 / 112;
        if (i) {
            cv_line(x - 2, mid - py * amp / 127, x, mid - w * amp / 127, T_DIM);
            if (amt)
                cv_line_t(x - 2, mid - py2 * amp * amt / 12700, x, mid - w * amp * amt / 12700, T_THEME, 2);
        }
        py = w;
        py2 = w;
    }
    dx = 10 + ph * 220 / 65536;                  /* the dot rides the wave now: the real one, from the audio */
    dy = mid - vnow * amp * (amt ? amt : 100) / (32767 * 100);
    cv_rect(dx, 4, 1, 74, T_TINT);
    sc_disc(dx, dy, 3, amt ? T_ACCENT : T_DIM);
    str_cpy(b, "", sizeof b);
    fixed2(b, rate, 2);
    str_cat(b, " HZ", sizeof b);
    cv_text_r(228, 6, &AF_S, b, T_MID, T_SURF);
    par_text(b, sizeof b, shape, F_LSHAPE);
    cv_text(12, 6, &AF_S, b, T_TEXT);
    if (!amt)
        cv_text_in(4, 64, 232, &AF_S, "DEPTH 0: IT MOVES NOTHING", T_MID, T_SURF);
    sw = vnow * amt / 100;                       /* what it does to the target now: -32 767 .. 32 767 */
    for (k = 0; k < 5; k++) {                    /* the five things it can move: the chosen one lit, with its own little gauge */
        int32_t x0 = 4 + k * 47, on = k == dest;
        cv_rrect(x0, 86, 44, 36, 4, on ? T_RAISE : T_SURF, T_BG);
        cv_text_in(x0, 90, 44, &AF_S, D[k], on ? T_ACCENT : T_DIM, on ? T_RAISE : T_SURF);
        cv_rect(x0 + 6, 110, 32, 1, on ? T_MID : T_LINE);
        if (on && amt) {
            int32_t g = sw * 16 / 32767;
            cv_rect(x0 + 22 + (g < 0 ? g : 0), 106, (g < 0 ? -g : g) + 1, 8, T_THEME);
        }
    }
    cv_blit(0, y);
}

static void sc_lfo2(uint32_t y)                  /* 240 x 124: BREATH's three, one a row */
{
    static const char *const N[3] = {"PITCH", "CUTOFF", "LEVEL"};
    int32_t d = cur.p[P_BREATH], w[3], r, i, x, py = 0;
    w[0] = cur.p[P_TOPITCH];
    w[1] = cur.p[P_TOCUT];
    w[2] = cur.p[P_TOLEVEL];
    cv_begin(240, 124, T_BG);
    for (r = 0; r < 3; r++) {
        int32_t y0 = 2 + r * 41, mid = y0 + 20, dw = d * w[r], amp = dw ? 2 + 15 * isq(dw) / 100 : 0, ph = (int32_t)(br.ph[r] >> 16) * 120 / 65536;
        char b[16];
        cv_rrect(4, y0, 232, 38, 4, T_SURF, T_BG);
        cv_text(10, y0 + 3, &AF_S, N[r], dw ? T_TEXT : T_DIM);
        if (r == 0) {                            /* what it does at full swing: +-8 cents, +-25 DARK steps, +-1.5 dB */
            str_cpy(b, "+-", sizeof b);
            fixed2(b + 2, 80 * dw / 10000, 1);
            str_cat(b, " CENTS", sizeof b);
        } else if (r == 1) {
            str_cpy(b, "+-", sizeof b);
            num(b + 2, 25 * dw / 10000);
            str_cat(b, " STEPS", sizeof b);
        } else {
            str_cpy(b, "+-", sizeof b);
            fixed2(b + 2, 15 * dw / 10000, 1);
            str_cat(b, " DB", sizeof b);
        }
        cv_text_r(230, y0 + 3, &AF_S, b, T_MID, T_SURF);
        cv_rect(60, mid, 168, 1, T_LINE);
        for (i = 0; i <= 84; i++) {
            x = 60 + i * 168 / 84;
            if (i)
                cv_line(x - 2, mid - py * (amp ? amp : 1) / 127, x, mid - sn(i * 3 + ph) * (amp ? amp : 1) / 127, amp ? T_THEME : T_DIM);
            py = sn(i * 3 + ph);
        }
        sc_disc(60, mid - sn(ph) * (amp ? amp : 1) / 127, 3, amp ? T_ACCENT : T_DIM);
    }
    cv_blit(0, y);
}

/* ---- EDIT ---- */
static void sc_edit(uint32_t y)                  /* 240 x 124 */
{
    int32_t t = (int32_t)(fm1_ms / 30u), x, py = 0, py2 = 0, mid = 44, amp = 34;
    char b[40];
    cv_begin(240, 124, T_BG);
    cv_rrect(4, 2, 232, 92, 4, T_SURF, T_BG);
    if (cur.engine == ENG_FM) {
        int32_t ratio = cur.p[P_RATIO], idx = cur.p[P_INDEX] * 115 / 100, fb = cur.p[P_FB];
        cv_rect(10, mid, 220, 1, T_LINE);
        for (x = 0; x <= 110; x++) {             /* the modulator (thin) and the carrier it bends (thick) */
            int32_t px = 10 + x * 2, ph = x * 120 / 50 + t, m = sn(ratio * x * 120 / 50 + t * ratio), s;
            s = sn(ph + idx * m / 127 + fb * sn(ph * 2) / 600);
            if (x) {
                cv_line(px - 2, 14 - py2 * 8 / 127, px, 14 - m * 8 / 127, T_DIM);
                cv_line_t(px - 2, mid + 6 - py * amp / 160, px, mid + 6 - s * amp / 160 * 1, T_THEME, 2);
            }
            py = s;
            py2 = m;
        }
        cv_text(12, 20, &AF_S, "MODULATOR", T_DIM);
        cv_text(12, 80, &AF_S, "CARRIER BENT BY IT", T_MID);
    } else {
        int32_t L = cur.p[P_LAYERS], sub = cur.p[P_SUB], so = cur.p[P_SUBOCT], sp = 2 + cur.p[P_MASSA] / 4 + cur.p[P_CURVE] / 8, l;
        uint16_t col[3] = {T_THEME, T_ACCENT, T_MID};
        cv_rect(10, mid, 220, 1, T_LINE);
        for (l = 0; l < L; l++) {                /* each layer a saw of its own speed: they drift apart and meet (the beating) */
            int32_t off = (int32_t)((t * (100 + (l - 1) * sp) / 100) % 56), p0 = 0;
            for (x = 0; x < 220; x++) {
                int32_t u = (x + off + l * 3) % 56, v = 26 - u * 52 / 56;
                if (x && u)
                    cv_line(9 + x, mid - 4 - p0, 10 + x, mid - 4 - v, col[l]);
                p0 = v;
            }
        }
        if (sub) {                               /* the sub: a sine one or two octaves down, as tall as SUB LEVEL */
            int32_t per = so == 2 ? 224 : 112, sa = 12 * sub / 100;
            for (x = 0; x <= 110; x++) {
                int32_t s = sn(x * 2 * 120 / per + t / 2), px = 10 + x * 2;
                if (x)
                    cv_line(px - 2, 74 - py * sa / 127, px, 74 - s * sa / 127, T_DIM);
                py = s;
            }
        }
        str_cpy(b, "", sizeof b);
        num(b, L);
        str_cat(b, L == 1 ? " SAW" : " SAWS DRIFTING", sizeof b);
        cv_text(12, 4, &AF_S, b, T_DIM);
        cv_text(12, 80, &AF_S, sub ? (so == 2 ? "SUB: TWO OCTAVES DOWN" : "SUB: ONE OCTAVE DOWN") : "NO SUB", T_MID);
    }
    cv_blit(0, y);
}

/* ---- GLO ---- */
static void sc_glo(uint32_t y)                   /* 240 x 124 */
{
    int32_t ch = set.g[G_CH], trs = set.g[G_TRS], vol = set.g[G_VOL], i, k;
    char b[16];
    cv_begin(240, 124, T_BG);
    for (i = 0; i < 3; i++) {
        int32_t x0 = 3 + i * 79;
        cv_rrect(x0, 2, 75, 120, 6, T_SURF, T_BG);
        cv_text_in(x0, 6, 75, &AF_S, i == 0 ? "MIDI CH" : i == 1 ? "TRS IN" : "MASTER", T_MID, T_SURF);
    }
    for (k = 0; k < 16; k++) {                   /* sixteen lamps: the channel lit (OMNI: all of them) */
        int32_t cx = 3 + 14 + (k % 4) * 16, cy = 36 + (k / 4) * 16, on = ch == 0 || ch == k + 1;
        sc_disc(cx, cy, 5, on ? (ch == 0 ? T_THEME : T_ACCENT) : T_RAISE);
    }
    if (ch)
        num(b, ch);
    par_text(b, sizeof b, ch, F_CH);
    cv_text_in(3, 104, 75, &AF_M, b, T_TEXT, T_SURF);
    sc_ring(82 + 38, 52, 20, trs ? T_THEME : T_DIM);   /* the jack: a socket and a plug */
    sc_ring(82 + 38, 52, 19, trs ? T_THEME : T_DIM);
    sc_disc(82 + 38, 52, 12, T_RAISE);
    sc_disc(82 + 38, 52, 5, trs ? T_ACCENT : T_BG);
    if (trs)
        for (i = 0; i < 3; i++)
            sc_ring(82 + 38, 52, 26 + i * 5, T_TINT);
    par_text(b, sizeof b, trs, F_ONOFF);
    cv_text_in(82, 104, 75, &AF_M, b, trs ? T_TEXT : T_DIM, T_SURF);
    {                                            /* the speaker: a cone and as many sound arcs as the volume gives */
        int32_t cx = 161 + 26, cy = 52, arcs = (vol + 19) / 20;
        cv_rect(cx - 24, cy - 6, 8, 12, T_MID);
        cv_line_t(cx - 16, cy - 6, cx - 4, cy - 18, T_MID, 2);
        cv_line_t(cx - 16, cy + 6, cx - 4, cy + 18, T_MID, 2);
        cv_line_t(cx - 4, cy - 18, cx - 4, cy + 18, T_MID, 2);
        for (k = 0; k < 5; k++)
            if (k < arcs) {
                int32_t r = 10 + k * 5;
                cv_line_t(cx + 2 + r / 2, cy - r / 2, cx + 2 + r / 2 + 3, cy, k < 4 ? T_THEME : T_ACCENT, 2);
                cv_line_t(cx + 2 + r / 2 + 3, cy, cx + 2 + r / 2, cy + r / 2, k < 4 ? T_THEME : T_ACCENT, 2);
            }
    }
    num(b, vol);
    cv_text_in(161, 104, 75, &AF_M, b, T_TEXT, T_SURF);
    cv_blit(0, y);
}

/* ---- the quasar's FX: the corona ---- */
static void sc_corona(uint32_t y)                /* 240 x 124 */
{
    int32_t drive = cur.p[P_DRIVE], shim = cur.p[P_CHORUS], bloom = cur.p[P_REVERB], lvl = cur.p[P_LEVEL];
    int32_t lev = sc_level(), t = (int32_t)(fm1_ms / 45u), cx = 120, cy = 62, rs = 5 + lvl / 14, i, j, k, sp = spin_pub * 120 / 65536;
    int32_t rx = 62, ry = 10, px, py, x0 = 0, y0 = 0, nr = bloom ? 1 + bloom / 30 : 0, rmax = 24 + bloom * 36 / 100;
    cv_begin(240, 124, T_BG);
    for (j = 0; j < nr; j++) {                   /* BLOOM: rings leaving the core, further and more of them as it goes up */
        int32_t rr = rs + 2 + (t * 2 + j * rmax / (nr + 1)) % (rmax - rs + 1), age = (rr - rs) * 3 / (rmax - rs + 1);
        sc_ring(cx, cy, rr > 58 ? 58 : rr, age == 0 ? T_ACCENT : age == 1 ? T_MID : T_DIM);
    }
    for (k = 0; k <= (shim ? 2 : 0); k++) {      /* SHIMMER: the disc doubled, tripled, the ghosts swinging against it */
        int32_t sw = k ? (sn(t * 3 + k * 40) * shim / 127) * 12 / 100 : 0, dy = k ? sn(t * 2 + k * 60) * shim * 6 / 12700 : 0;
        for (i = 0; i <= 60; i++) {
            int32_t a = i * 2;
            px = cx + (rx + sw) * cs(a) / 127;
            py = cy + dy - ry * sn(a) / 127;
            if (i)
                cv_line_t(x0, y0, px, py, k ? T_MID : T_THEME, k ? 1 : cs(a) < 0 ? 2 : 1);
            x0 = px;
            y0 = py;
        }
    }
    for (k = 0; k < 3; k++) {                    /* SPIN: three hot knots racing round the disc */
        int32_t a = sp + k * 40;
        sc_disc(cx + rx * cs(a) / 127, cy - ry * sn(a) / 127, cs(a) < 0 ? 3 : 2, T_ACCENT);
    }
    for (k = -1; k <= 1; k += 2)                 /* FUZZ: the jets burn: a ragged edge, hotter and wider with DRIVE, squared off */
        for (j = 0; j < 44; j += 2) {
            int32_t jag = sn(j * 5 + t * 4 + (k > 0 ? 30 : 0)) > 0 ? 1 : -1, w = 2 + j / 6 + drive / 25;
            int32_t jx = cx + jag * (drive * (4 + j / 10) / 100) + sn(sp + j * 2) * (j / 4) / 127;
            cv_rect(jx - w / 2, k < 0 ? cy - rs - j - 2 : cy + rs + j, w, 2, j < 14 ? T_ACCENT : j < 30 ? T_THEME : T_MID);
        }
    sc_disc(cx, cy, rs + 3 + drive / 25 + lev / 60, T_TINT);
    sc_disc(cx, cy, rs, T_THEME);
    sc_disc(cx, cy, rs - 2, T_TEXT);
    cv_text(6, 4, &AF_S, "FUZZ = JETS", T_DIM);
    cv_text_r(234, 4, &AF_S, "SHIMMER = GHOSTS", T_DIM, T_BG);
    cv_text(6, 109, &AF_S, "BLOOM = RINGS", T_DIM);
    cv_text_r(234, 109, &AF_S, "LEVEL = CORE", T_DIM, T_BG);
    cv_blit(0, y);
}

/* ---- GEN (SCL 2) ---- */
static void sc_gen(uint32_t y)                   /* 240 x 124 */
{
    static int16_t tx[10], ty[10];               /* the drift's trail: a new point every picture */
    static uint8_t tn;
    int32_t cx = 62, cy = 62, life = set.g[G_XLIFE], span = set.g[G_XSPAN], sc = set.g[G_SCALE], len, r, k, i, a, px, py;
    uint32_t keys = gen_pub_keys & ~0u, held = 0;
    char b[24], t[12];
    static const uint8_t PENT[5] = {0, 3, 5, 7, 10};
    len = sc >= 1 && sc <= 6 ? (int32_t)SC_LEN[sc] : 5;
    for (i = 0; i < (int32_t)NV; i++)            /* what the generator holds and still sounds */
        if (snd.held[i]) {
            int32_t kk = note_key(snd.held[i]);
            if (kk >= 0)
                held |= 1u << kk;
        }
    keys &= held;
    cv_begin(240, 124, T_BG);
    for (k = 0; k < span; k++) {                 /* one ring an octave, a dot a degree */
        r = 8 + (k + 1) * 44 / span;
        sc_ring(cx, cy, r, T_LINE);
        for (i = 0; i < len; i++) {
            int32_t key = sc >= 1 && sc <= 6 ? k * len + i : 12 * k + PENT[i];
            int32_t on = key <= 26 && ((keys >> key) & 1u);
            a = 30 - i * 120 / len;
            px = cx + (r * cs(a) >> 7);
            py = cy - (r * sn(a) >> 7);
            if (key > 26)
                continue;
            sc_disc(px, py, on ? 5 : 2, on ? T_ACCENT : T_DIM);
        }
    }
    if (life) {                                  /* the hand: once round to the next move */
        a = 30 - (int32_t)gen_pub_prog * 120 / 255;
        cv_line_t(cx, cy, cx + (52 * cs(a) >> 7), cy - (52 * sn(a) >> 7), T_THEME, 1);
    }
    sc_disc(cx, cy, 3, life ? T_THEME : T_DIM);
    if (life) {
        int32_t ms = 20000 / (1 + life * life / 400);
        str_cpy(b, "EVERY ", sizeof b);
        num(t, ms / 1000);
        str_cat(b, t, sizeof b);
        str_cat(b, ".", sizeof b);
        num(t, ms / 100 % 10);
        str_cat(b, t, sizeof b);
        str_cat(b, " S", sizeof b);
        cv_text(130, 2, &AF_M, b, T_THEME);
        if (set.g[G_CHORD] > 0 || set.g[G_LOW] > 0)
            str_cpy(b, "CHANGES CHORDS", sizeof b);
        else {
            str_cpy(b, "HOLDS ", sizeof b);
            num(t, set.g[G_XNOTES]);
            str_cat(b, t, sizeof b);
            str_cat(b, set.g[G_XNOTES] == 1 ? " NOTE" : " NOTES", sizeof b);
        }
        cv_text(130, 22, &AF_S, b, T_MID);
    } else {
        cv_text(130, 2, &AF_M, "ASLEEP", T_DIM);
        cv_text(130, 22, &AF_S, "LIFE UP TO START", T_MID);
    }
    cv_rrect(128, 42, 108, 78, 6, T_SURF, T_BG);   /* the drift field: the cutoff across, the fuzz up; the spin rings the dot */
    cv_text(134, 46, &AF_S, "DRIFT", T_DIM);
    px = 182 + GDR(gdr.cut) * 46 / 30;
    py = 84 - GDR(gdr.gain) * 26 / 8200;
    px = px < 136 ? 136 : px > 228 ? 228 : px;
    py = py < 58 ? 58 : py > 112 ? 112 : py;
    if (fm1_ms / 100u != (uint32_t)tn) {
        for (i = 9; i > 0; i--) {
            tx[i] = tx[i - 1];
            ty[i] = ty[i - 1];
        }
        tx[0] = (int16_t)px;
        ty[0] = (int16_t)py;
        tn = (uint8_t)(fm1_ms / 100u);
    }
    for (i = 9; i > 0; i--)
        if (tx[i] && set.g[G_XDRIFT])
            sc_disc(tx[i], ty[i], 1 + (i < 4), i < 4 ? T_MID : T_DIM);
    if (set.g[G_XDRIFT]) {
        int32_t ro = 5 + (gdr.spin < 0 ? -gdr.spin : gdr.spin) / 65536 / 5;
        sc_disc(px, py, 3, T_ACCENT);
        sc_ring(px, py, ro > 12 ? 12 : ro, T_THEME);
    } else
        cv_text_in(128, 78, 108, &AF_S, "STILL", T_DIM, T_SURF);
    cv_blit(0, y);
}
