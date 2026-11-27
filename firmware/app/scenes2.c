/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Vlad Kuznetsov */
/* More pictures (main loop), after scenes.c: the black hole on HOME, the cassette, the pads' legend, the grains, the
 * scale wheel and SHOEGAZE's pedalboard. Same rules: one canvas (240 x 124 at most), integer maths, drawn every
 * ~70 ms when they move with the sound or the tape, and only when a value changes when they do not.
 *
 * The black hole is the drone: its ring is the scope of what comes out wrapped round a circle (the wave is the
 * ring's radius), the stars are pulled outwards the way a lens does it, with a strength that follows the level and
 * MASSA (the hole's size), BREATH sets how fast the whole thing breathes, DARK how far its halos reach. In SHOEGAZE
 * it is not dark at all: a core of light with halos in place of the shadow. */
static const int8_t C120[120] = {
    127, 127, 126, 125, 124, 123, 121, 119, 116, 113, 110, 107, 103, 99, 94, 90, 85, 80, 75, 69,
    64, 58, 52, 46, 39, 33, 26, 20, 13, 7, 0, -7, -13, -20, -26, -33, -39, -46, -52, -58,
    -63, -69, -75, -80, -85, -90, -94, -99, -103, -107, -110, -113, -116, -119, -121, -123, -124, -125, -126, -127,
    -127, -127, -126, -125, -124, -123, -121, -119, -116, -113, -110, -107, -103, -99, -94, -90, -85, -80, -75, -69,
    -64, -58, -52, -46, -39, -33, -26, -20, -13, -7, 0, 7, 13, 20, 26, 33, 39, 46, 52, 58,
    64, 69, 75, 80, 85, 90, 94, 99, 103, 107, 110, 113, 116, 119, 121, 123, 124, 125, 126, 127,
};
static int32_t cs(int32_t i) { return C120[(i % 120 + 120) % 120]; }          /* cos of i x 3 degrees, x 127 */
static int32_t sn(int32_t i) { return C120[((i + 90) % 120 + 120) % 120]; }   /* sin */

static const int8_t STARS[32][2] = {{-98, -29}, {-88, -6}, {-98, 22}, {-94, 38}, {-77, -30}, {-67, 0}, {-63, 11}, {-79, 46}, {-43, -42}, {-50, -3}, {-30, 26}, {-51, 51}, {-10, -32}, {-2, -1}, {-2, 11}, {-3, 32}, {24, -44}, {9, -19}, {14, 13}, {27, 32}, {62, -32}, {48, -6}, {56, 12}, {54, 39}, {88, -37}, {83, -20}, {89, 8}, {82, 52}, {106, -33}, {115, -18}, {120, 14}, {108, 39}};

static struct {
    uint32_t t, cph;             /* the cassette: when it was drawn, the reels' angle (Q8 of 3 degrees) */
} sc2;

static int32_t sc_scope_max(void)                /* the scope's peak (at least 64) */
{
    int32_t m = 64, a;
    uint32_t i;
    for (i = 0; i < SCOPE_N; i++) {
        a = scope.y[i] < 0 ? -scope.y[i] : scope.y[i];
        m = a > m ? a : m;
    }
    return m;
}

static uint16_t sc_hot(int32_t lev)              /* the ring's colour: the theme, the accent when it is loud */
{
    return set.g[G_MODE] == MODE_SHOE || lev > 200 ? T_ACCENT : T_THEME;
}

static void sc_hole(uint32_t y)                  /* HOME: 240 x 104 */
{
    int32_t lev = sc_level(), cx = 120, cy = 44, mas = cur.p[P_MASSA], br = cur.p[P_BREATH], dark = cur.p[P_DARK];
    int32_t t = (int32_t)(fm1_ms / 50u), pul = cs(t * (1 + br / 40)), shoe = set.g[G_MODE] == MODE_SHOE;
    int32_t rs, R, A, m, i, k, px, py, x0 = 0, y0 = 0, lens;
    uint8_t n[NV];
    rs = 12 + mas * 10 / 100 + lev / 64 + (pul > 0 ? pul / 64 : 0);
    R = rs + 4 + pul * 2 / 127;
    A = 2 + lev / 40;
    lens = rs * rs * 2;
    m = sc_scope_max();
    cv_begin(240, 104, T_BG);
    for (i = 0; i < 32; i++) {                   /* the stars, pulled out by the lens */
        int32_t sx = STARS[i][0], sy = STARS[i][1], d2 = sx * sx + sy * sy, f = 256 + 256 * lens / (d2 + lens);
        int32_t tw = (t / 3 + i * 5) % 7, big = lev > 150 && tw == 0;
        uint16_t c = tw == 0 ? T_TEXT : tw < 4 ? T_MID : T_DIM;
        px = cx + (sx * f >> 8);
        py = cy + (sy * f >> 8);
        if (px < 1 || px > 237 || py < 1 || py > 101)
            continue;
        if (shoe) {                              /* sparkles */
            cv_rect(px - 1, py, 3, 1, c);
            cv_rect(px, py - 1, 1, 3, c);
        } else
            cv_rect(px, py, big ? 2 : 1, big ? 2 : 1, c);
    }
    if (shoe) {                                  /* a core of light */
        sc_disc(cx, cy, R + 12 + dark / 12, T_TINT);
        sc_disc(cx, cy, R + 7, T_RAISE);
        sc_disc(cx, cy, R + 3, T_THEME);
    } else {                                     /* halos: they reach further with DARK and open with the level */
        for (k = 0; k < 3; k++)
            if (lev > 20 + 40 * k || k == 0)
                sc_ring(cx, cy, R + 7 + 7 * k + pul * (k + 1) / 127 + dark / 25, k == 0 ? T_LINE : T_TINT);
    }
    for (i = 0; i <= 120; i++) {                 /* the ring: the wave, round */
        int32_t j = i % 120, r = R + scope.y[(uint32_t)(j * 2) % SCOPE_N] * A / m;
        px = cx + r * cs(j) / 127;
        py = cy - r * sn(j) / 127;
        if (i)
            cv_line_t(x0, y0, px, py, sc_hot(lev), 2);
        x0 = px;
        y0 = py;
    }
    if (shoe)
        sc_disc(cx, cy, rs - 2, T_TEXT);         /* the core */
    else {
        sc_ring(cx, cy, rs + 1, T_TEXT);         /* the photon ring, then the shadow */
        sc_disc(cx, cy, rs, T_BG);
    }
    for (i = 0; i < (int32_t)NV; i++)
        n[i] = snd.held[i];
    for (i = 0; i < (int32_t)NV; i++) {          /* the held notes along the bottom */
        char b[8];
        if (!n[i])
            continue;
        note_name(b, n[i]);
        cv_text(i * 60 + (60 - text_w(&AF_M, b)) / 2, 86, &AF_M, b, T_TEXT);
    }
    cv_blit(0, y);
}

/* ---- the cassette (TAPE) ---- */
static uint16_t tape_col(void)
{
    return tape.st == TP_REC || tape.st == TP_DUB ? T_REC : tape.st == TP_PLAY ? T_THEME : T_MID;
}

static void sc_cassette(uint32_t y)              /* 240 x 124 */
{
    int32_t pct = tape.st == TP_EMPTY ? 50 : (int32_t)tape_pct(), spd = (int32_t)tape.spd_pub, dt, k, rl, rr, sem;
    int32_t rev = (set.g[G_DIR] != 0) != ((tape.pads >> PD_REV & 1u) != 0u), lc = 86, rc = 154, cy = 74;
    char b[24], v[16];
    dt = (int32_t)(fm1_ms - sc2.t);
    dt = dt < 0 || dt > 200 || !sc2.t ? 70 : dt;
    sc2.t = fm1_ms;
    sc2.cph += (uint32_t)(dt * (spd >> 4) / 160);              /* the reels turn with the speed (1x: ~1 turn a second) */
    rl = 11 + (100 - pct) * 13 / 100;
    rr = 11 + pct * 13 / 100;
    cv_begin(240, 124, T_BG);
    cv_rrect(14, 2, 212, 118, 8, T_SURF, T_BG);                /* the cassette, its label and its window */
    cv_rrect(26, 8, 188, 38, 4, T_BG, T_SURF);
    cv_rrect(52, 50, 136, 62, 10, T_BG, T_SURF);
    cv_text(34, 12, &AF_M, TP_NAME[tape.st], tape_col());
    num(b, (int32_t)tape.pos / 100);                           /* the position / the length, in seconds and tenths */
    str_cpy(v, b, sizeof v);
    if (tape.len) {
        str_cpy(b, "", sizeof b);
        num(b, (int32_t)tape.pos / 1000);
        str_cat(b, ".", sizeof b);
        num(v, (int32_t)tape.pos / 100 % 10);
        str_cat(b, v, sizeof b);
        str_cat(b, " / ", sizeof b);
        num(v, (int32_t)tape.len / 1000);
        str_cat(b, v, sizeof b);
        str_cat(b, " S", sizeof b);
        cv_text_r(206, 14, &AF_S, b, T_TEXT, T_BG);
    }
    num(b, spd * 100 / 65536);                                 /* the speed, the pitch and the artifacts */
    str_cat(b, " %", sizeof b);
    cv_text(34, 29, &AF_S, b, T_MID);
    sem = set.g[G_PITCH] + ((tape.pads >> PD_UP & 1u) ? 12 : 0) - ((tape.pads >> PD_DOWN & 1u) ? 12 : 0) +
          ((tape.pads >> PD_FIFTH & 1u) ? 7 : 0);
    if (sem || set.g[G_LOCK]) {
        b[0] = 0;
        if (set.g[G_LOCK])
            str_cat(b, "LOCK ", sizeof b);
        if (sem) {
            par_text(v, sizeof v, sem, F_PITCH);
            str_cat(b, v, sizeof b);
        }
        cv_text(96, 29, &AF_S, b, T_THEME);
    }
    if (tape.pads >> PD_CHEW & 1u)
        cv_text_r(206, 29, &AF_S, "CHEW", T_ACCENT, T_BG);
    else if (tape.def > 3000)
        cv_text_r(206, 29, &AF_S, "DROPOUT", T_REC, T_BG);
    else if (tape.gon)
        cv_text_r(206, 29, &AF_S, "GRAINS", T_THEME, T_BG);
    for (k = 0; k < 2; k++) {                                  /* the reels: the tape's pack, the hub, three spokes */
        int32_t cx = k ? rc : lc, r = k ? rr : rl, s;
        sc_disc(cx, cy, r, T_TINT);
        sc_ring(cx, cy, r, T_DIM);
        sc_disc(cx, cy, 9, T_RAISE);
        for (s = 0; s < 6; s++) {
            int32_t a = (int32_t)(sc2.cph >> 8) * (rev ? -1 : 1) + 20 * s;
            cv_line_t(cx, cy, cx + cs(a) * 9 / 127, cy - sn(a) * 9 / 127, T_MID, 2);
        }
        sc_disc(cx, cy, 3, T_BG);
    }
    cv_line_t(lc - 4, cy + rl, 112, 106, T_MID, 1);            /* the tape over the head */
    cv_line_t(112, 106, 128, 106, T_MID, 1);
    cv_line_t(128, 106, rc + 4, cy + rr, T_MID, 1);
    cv_blit(0, y);
}

/* ---- the pads (TAPE 2): SEQ held, the keys are these ---- */
static void sc_pads(uint32_t y)                  /* 240 x 124 */
{
    int32_t i;
    cv_begin(240, 124, T_BG);
    cv_text(8, 0, &AF_S, "HOLD SEQ, PLAY THE KEYS", T_MID);
    for (i = 0; i < (int32_t)NPAD; i++) {
        int32_t col = i / 5, row = i % 5, x = 3 + col * 79, yy = 16 + row * 21;
        int on = i == PD_ECHO ? dl.freeze != 0u : ((tape.pads >> i) & 1u) != 0u;
        char b[16];
        num(b, i + 1);
        str_cat(b, " ", sizeof b);
        str_cat(b, PAD_NAME[i], sizeof b);
        cv_rrect(x, yy, 76, 19, 3, on ? T_THEME : T_SURF, T_BG);
        cv_text(x + 5, yy + 3, &AF_S, b, on ? T_INK : T_TEXT);
    }
    cv_blit(0, y);
}

/* ---- the grains (DELAY 3, 4): the tape line with the grains moving over it ---- */
static void sc_grains(uint32_t y)                /* 240 x 124 */
{
    int32_t x, j, sz = set.g[G_GSIZE], scat = set.g[G_GSCAT], L = (20 + sz * 6) * 441 / 10, lo, hi, live = 0;
    uint32_t w = dl.w_pub;
    char b[24];
    lo = 64 + (L >> 3);
    hi = lo + (32000 - lo) * scat / 100;
    cv_begin(240, 124, T_BG);
    cv_text(8, 0, &AF_S, "GRAIN DELAY", T_MID);
    par_text(b, sizeof b, sz, F_GMS);
    cv_text_r(232, 0, &AF_M, b, T_TEXT, T_BG);
    cv_rect(8, 62, 224, 1, T_LINE);
    for (x = 0; x < 224; x++) {                                /* what is on the tape: the newest at the right */
        int32_t m = 0, s;
        for (s = 0; s < 6; s++) {
            int32_t c = dl_cells[(w - 1u - (uint32_t)((223 - x) * 146 + s * 24)) & DL_MASK];
            c = c < 0 ? -c : c;
            m = c > m ? c : m;
        }
        s = sc_bits(m) - 5;
        if (s > 0)
            cv_rect(8 + x, 62 - s * 2, 1, 4 * s + 1, T_DIM);
    }
    cv_rrect(226, 40, 12, 44, 2, T_REC, T_BG);                 /* the record head, where the tape is written */
    cv_text_in(226, 55, 12, &AF_S, "R", T_INK, T_REC);
    for (j = 0; j < (int32_t)DL_NG; j++) {                     /* the grains: a bar as tall as its window is open */
        int32_t gw = dl.gp_w[j], gx, h;
        if (!gw)
            continue;
        live++;
        gx = 224 - (dl.gp_gap[j] * 224 >> 15);
        h = (gw < 0 ? -gw : gw) * 36 / 255;
        cv_rect(8 + gx - 1, 62 - h, 3, 2 * h + 1, gw < 0 ? T_REC : T_ACCENT);
        cv_rect(8 + gx - 3, 62 + h, 7, 1, gw < 0 ? T_REC : T_ACCENT);
        cv_rect(8 + gx - 3, 62 - h, 7, 1, gw < 0 ? T_REC : T_ACCENT);
    }
    {                                                          /* where grains may start: SIZE puts the nearest, SCATTER the rest */
        int32_t xl = 232 - (lo * 224 >> 15), xh = 232 - (hi * 224 >> 15);
        cv_rect(xh, 98, xl - xh + 1, 2, T_THEME);
        cv_rect(xh, 94, 1, 10, T_THEME);
        cv_rect(xl, 94, 1, 10, T_THEME);
        cv_text_in(8, 106, 224, &AF_S, "WHERE THE GRAINS START", T_MID, T_BG);
    }
    if (!set.g[G_GMIX])
        cv_text_in(0, 24, 240, &AF_M, "GRAINS OFF (KNOB 1)", T_DIM, T_BG);
    else {
        num(b, live);
        str_cat(b, " GRAINS", sizeof b);
        cv_text(8, 17, &AF_S, b, T_THEME);
    }
    cv_blit(0, y);
}

/* ---- the scale wheel (SCL): the twelve notes round a circle ---- */
static void sc_scl(uint32_t y)                   /* 240 x 124 */
{
    int32_t cx = 62, cy = 62, r = 48, p, d, i, n = 0, ph[8], sc = set.g[G_SCALE], root = set.g[G_ROOT];
    uint32_t held = 0, inm = 0;
    int32_t notes[NV];
    char b[48], t[8];
    static const char *const PC[12] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
    cv_begin(240, 124, T_BG);
    sc_ring(cx, cy, r + 11, T_LINE);
    if (sc == 0)
        inm = 0xFFFu;
    else
        for (d = 0; d < (int32_t)SC_LEN[sc]; d++)
            inm |= 1u << ((root + SC_DEG[sc][d]) % 12);
    for (i = 0; i < (int32_t)NV; i++)
        if (snd.held[i])
            held |= 1u << (snd.held[i] % 12u);
    if (held) {                                                /* the sounding chord, else a preview over the root */
        for (i = 0; i < (int32_t)NV; i++)
            if (snd.held[i])
                notes[n++] = snd.held[i];
    } else
        n = (int32_t)chord_notes(scale_start(), notes);
    for (i = 0; i < n && i < 8; i++)
        ph[i] = notes[i] % 12;
    for (i = 0; i < n; i++) {                                  /* the chord's shape (a held one bright) */
        int32_t a = 30 - ph[i] * 10, b2 = 30 - ph[(i + 1) % n] * 10;
        if (n > 1 && (i + 1 < n || n > 2))
            cv_line_t(cx + (r * cs(a) >> 7), cy - (r * sn(a) >> 7), cx + (r * cs(b2) >> 7), cy - (r * sn(b2) >> 7),
                      held ? T_ACCENT : T_MID, held ? 2 : 1);
    }
    for (p = 0; p < 12; p++) {
        int32_t a = 30 - p * 10, px = cx + (r * cs(a) >> 7), py = cy - (r * sn(a) >> 7);
        if ((inm >> p) & 1u) {
            sc_disc(px, py, 9, (held >> p) & 1u ? T_ACCENT : T_THEME);
            cv_text_in(px - 9, py - 6, 18, &AF_S, PC[p], T_INK, (held >> p) & 1u ? T_ACCENT : T_THEME);
        } else
            sc_disc(px, py, 2, T_DIM);
    }
    {
        int32_t a = 30 - root * 10;
        sc_ring(cx + (r * cs(a) >> 7), cy - (r * sn(a) >> 7), 12, sc ? T_TEXT : T_DIM);
    }
    par_text(b, sizeof b, sc, F_SCALE);
    cv_text(130, 2, &AF_M, b, T_THEME);
    str_cpy(b, "ROOT ", sizeof b);
    str_cat(b, PC[root % 12], sizeof b);
    if (set.g[G_LOW]) {                                        /* (the LOW root rides on the root's line) */
        str_cat(b, "  ", sizeof b);
        par_text(t, sizeof t, set.g[G_LOW], F_LOW);
        str_cat(b, t, sizeof b);
    }
    cv_text(130, 22, &AF_S, sc ? b : "ANY ROOT", T_MID);
    b[0] = 0;
    if (sc == 0)
        str_cpy(b, "ALL TWELVE", sizeof b);
    else
        for (d = 0; d < (int32_t)SC_LEN[sc]; d++) {
            if (d)
                str_cat(b, " ", sizeof b);
            str_cat(b, PC[(root + SC_DEG[sc][d]) % 12], sizeof b);
        }
    cv_text(130, 38, &AF_S, b, T_TEXT);
    par_text(b, sizeof b, set.g[G_CHORD], F_CHORD);
    cv_text(130, 58, &AF_M, b, T_ACCENT);
    b[0] = 0;
    for (i = 0; i < (int32_t)CH_LEN[set.g[G_CHORD] > 7 ? 0 : set.g[G_CHORD]]; i++) {
        if (i)
            str_cat(b, " ", sizeof b);
        num(t, CH_IV[set.g[G_CHORD] > 7 ? 0 : set.g[G_CHORD]][i]);
        str_cat(b, t, sizeof b);
    }
    cv_text(130, 78, &AF_S, b, T_MID);
    {                                                          /* what sounds: two notes a row (four would not fit 110 px) */
        int32_t row = 0, cnt = 0;
        b[0] = 0;
        for (i = 0; i < (int32_t)NV; i++) {
            if (!snd.held[i])
                continue;
            note_name(t, snd.held[i]);
            if (b[0])
                str_cat(b, " ", sizeof b);
            str_cat(b, t, sizeof b);
            if (++cnt % 2 == 0) {
                cv_text(130, 98 + row * 11, &AF_S, b, T_TEXT);
                row++;
                b[0] = 0;
            }
        }
        if (b[0]) {
            cv_text(130, 98 + row * 11, &AF_S, b, T_TEXT);
            row++;
        }
        if (!cnt)
            cv_text(130, 98, &AF_S, "NOTHING LATCHED", T_DIM);
        if (row < 2)
            cv_text(130, 98 + (row ? row : 1) * 11, &AF_S, "HOLD SCL: SILENCE", T_DIM);
    }
    cv_blit(0, y);
}

/* ---- SHOEGAZE's pedalboard (FX): fuzz, chorus, reverb, level ---- */
static void sc_pedals(uint32_t y)                /* 240 x 124 */
{
    static const char *const NAME[4] = {"FUZZ", "CHORUS", "REVERB", "LEVEL"};
    int32_t v[4], i, xx, lev = sc_level(), t = (int32_t)(fm1_ms / 40u);
    char b[8];
    v[0] = cur.p[P_DRIVE];
    v[1] = cur.p[P_CHORUS];
    v[2] = cur.p[P_REVERB];
    v[3] = cur.p[P_LEVEL];
    cv_begin(240, 124, T_BG);
    for (i = 0; i < 4; i++) {
        int32_t x0 = 3 + i * 59, mx = x0 + 27, on = i == 3 ? 1 : v[i] > 0;
        cv_rrect(x0, 2, 55, 120, 6, T_SURF, T_BG);
        cv_text_in(x0, 5, 55, &AF_S, NAME[i], on ? T_TEXT : T_DIM, T_SURF);
        sc_dial(mx, 36, 13, v[i] * 32 / 100, T_DIM, T_RAISE, on ? T_THEME : T_DIM);
        num(b, v[i]);
        cv_text_in(x0, 52, 55, &AF_S, b, T_TEXT, T_SURF);
        cv_rrect(x0 + 4, 68, 47, 38, 3, T_BG, T_SURF);
        if (i == 0) {                                           /* a sine, clipped harder as DRIVE goes up */
            int32_t clip = 17 - v[0] * 13 / 100, py0 = 0;
            for (xx = 0; xx < 47; xx++) {
                int32_t s = sn(xx * 120 / 47) * 17 / 127 * (1 + v[0] / 25);
                s = s > clip ? clip : s < -clip ? -clip : s;
                if (xx)
                    cv_line(x0 + 4 + xx - 1, 87 - py0, x0 + 4 + xx, 87 - s, T_THEME);
                py0 = s;
            }
        } else if (i == 1) {                                    /* two sines, one swung against the other */
            int32_t py0 = 0, py1 = 0, sw = v[1] * 14 / 100;
            for (xx = 0; xx < 47; xx++) {
                int32_t s0 = sn(xx * 3 + t) * 10 / 127, s1 = sn(xx * 3 + t + (sn(t * 2) * sw >> 7) + sw) * 10 / 127;
                if (xx) {
                    cv_line(x0 + 4 + xx - 1, 87 - py0, x0 + 4 + xx, 87 - s0, T_THEME);
                    if (v[1])
                        cv_line(x0 + 4 + xx - 1, 87 - py1, x0 + 4 + xx, 87 - s1, T_ACCENT);
                }
                py0 = s0;
                py1 = s1;
            }
        } else if (i == 2) {                                    /* ripples: more of them, and further, with REVERB */
            int32_t rmax = 4 + v[2] * 18 / 100, j;
            for (j = 0; j < 3 && v[2]; j++) {
                int32_t rr = (t / 2 + j * rmax / 3) % rmax;
                if (rr > 1)
                    sc_ring(mx, 87, rr > 17 ? 17 : rr, j == 0 ? T_THEME : T_MID);
            }
        } else {                                                /* a meter: the drone's level */
            int32_t h = lev * 32 / 256;
            cv_rect(x0 + 18, 102 - h, 19, h, lev > 200 ? T_ACCENT : T_THEME);
        }
        sc_disc(mx, 114, 4, on ? T_REC : T_DIM);
    }
    cv_blit(0, y);
}

static void sc_pedals_low(uint32_t y)            /* 240 x 64: what comes out, big */
{
    int32_t m = sc_scope_max(), i, y0, y1;
    cv_begin(240, 64, T_BG);
    cv_rect(8, 32, 224, 1, T_LINE);
    if (scope.count) {
        y0 = 32 - scope.y[0] * 26 / m;
        for (i = 1; i < (int32_t)SCOPE_N; i++) {
            y1 = 32 - scope.y[i] * 26 / m;
            cv_line(8 + (i - 1) * 224 / ((int32_t)SCOPE_N - 1), y0, 8 + i * 224 / ((int32_t)SCOPE_N - 1), y1, T_ACCENT);
            y0 = y1;
        }
    }
    cv_blit(0, y);
}
